#include "McpController.h"
#include "Files.h"
#include "HttpUtil.h"
#include "RoomHub.h"
#include "Rooms.h"

#include <drogon/utils/Utilities.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>

using http::json;

namespace
{
constexpr const char *kProtocolVersions[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

constexpr const char *kInstructions =
    "Rooms are shared spaces where agents (and people) coordinate work asynchronously. "
    "Each room has a message log and a list of todos.\n"
    "- Start with read_room to see the topic, who is around, the todos and recent messages.\n"
    "- Claim a todo before working on it (claim_todo). A claim held by someone else fails: "
    "pick another one or ask in the room. Release it if you stop.\n"
    "- Post short updates and questions with post_message; mark work finished with "
    "complete_todo.\n"
    "- To wait for others, call wait_for_messages with the cursor from your last read: it "
    "returns as soon as something new is posted.\n"
    "Changes to room todos (added, claimed, finished...) show up in the log as events.\n"
    "- Messages can carry attachments (images, PDFs): look at one with get_file, and attach "
    "your own by uploading it with upload_file and passing its id to post_message.";

// ── JSON-RPC ──

Json::Value rpcResult(const Json::Value &id, const Json::Value &result)
{
    Json::Value v;
    v["jsonrpc"] = "2.0";
    v["id"] = id;
    v["result"] = result;
    return v;
}

Json::Value rpcError(const Json::Value &id, int code, const std::string &msg)
{
    Json::Value v;
    v["jsonrpc"] = "2.0";
    v["id"] = id;
    v["error"]["code"] = code;
    v["error"]["message"] = msg;
    return v;
}

// A tools/call result: the text the agent reads, flagged if it's an error.
Json::Value toolText(const std::string &text, bool isError = false)
{
    Json::Value v;
    Json::Value c;
    c["type"] = "text";
    c["text"] = text;
    v["content"].append(c);
    v["isError"] = isError;
    return v;
}

Json::Value toolJson(const Json::Value &body)
{
    Json::StreamWriterBuilder w;
    w["indentation"] = " ";
    return toolText(Json::writeString(w, body));
}

// A tool argument the agent got wrong.
struct ArgError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// ── Tool definitions ──

Json::Value prop(const char *type, const char *description)
{
    Json::Value p;
    p["type"] = type;
    p["description"] = description;
    return p;
}

Json::Value roomProp()
{
    Json::Value p;
    p["type"] = Json::Value(Json::arrayValue);
    p["type"].append("string");
    p["type"].append("integer");
    p["description"] = "Room name or id";
    return p;
}

Json::Value tool(const char *name, const char *description,
                 std::vector<std::pair<const char *, Json::Value>> props,
                 std::vector<const char *> required)
{
    Json::Value t;
    t["name"] = name;
    t["description"] = description;
    auto &schema = t["inputSchema"];
    schema["type"] = "object";
    schema["properties"] = Json::Value(Json::objectValue);
    for (auto &[key, p] : props)
        schema["properties"][key] = p;
    schema["properties"]["agent"] = prop(
        "string", "Your name in rooms, if the server connection doesn't set one (X-Todo-Agent).");
    schema["required"] = Json::Value(Json::arrayValue);
    for (auto r : required)
        schema["required"].append(r);
    return t;
}

Json::Value toolList()
{
    Json::Value tools(Json::arrayValue);
    tools.append(tool("list_rooms", "List rooms with their topics, members and open todo counts.",
                      {}, {}));
    tools.append(tool("create_room", "Create a room for coordinating a piece of work.",
                      {{"name", prop("string", "Room name, unique")},
                       {"topic", prop("string", "What the room is for")}},
                      {"name"}));
    tools.append(tool(
        "read_room",
        "Read a room: topic, members, its todos (with who claimed them) and messages after "
        "`since`. Returns `cursor`, the value to pass as `since` next time.",
        {{"room", roomProp()},
         {"since", prop("integer", "A cursor: only messages after it. Omit for the latest ones.")},
         {"limit", prop("integer", "At most this many messages (default 50)")}},
        {"room"}));
    tools.append(tool(
        "wait_for_messages",
        "Block until a room has messages after `since` (or `timeout` seconds pass), then return "
        "them with the new `cursor`. Use this to wait for other agents instead of polling.",
        {{"room", roomProp()},
         {"since", prop("integer", "Cursor from your last read_room / wait_for_messages")},
         {"timeout", prop("number", "Seconds to wait, at most 300 (default 60)")}},
        {"room", "since"}));
    Json::Value ids = prop("array", "Files to attach: {id, name} as upload_file returned them");
    ids["items"]["type"] = "object";
    ids["items"]["properties"]["id"] = prop("string", "The file's id");
    ids["items"]["properties"]["name"] = prop("string", "The name to show");
    ids["items"]["required"].append("id");
    tools.append(tool("post_message", "Post a message to a room, optionally with attachments.",
                      {{"room", roomProp()},
                       {"text", prop("string", "The message (may be empty with attachments)")},
                       {"attachments", ids}},
                      {"room"}));
    tools.append(tool("upload_file",
                      "Upload an image (PNG, JPEG, GIF, WebP) or PDF, at most 10 MB, or a video "
                      "(MP4, WebM), at most 50 MB, to attach to a message. Returns its id.",
                      {{"name", prop("string", "File name, e.g. plot.png")},
                       {"data", prop("string", "The file's bytes, base64-encoded")}},
                      {"name", "data"}));
    tools.append(tool("get_file",
                      "Get a message's attachment by id: images come back as images, PDFs as "
                      "an embedded resource, videos as a description (they're too big).",
                      {{"id", prop("string", "The attachment's id")}}, {"id"}));
    tools.append(tool(
        "add_todo", "Add a todo to a room, optionally claiming it right away.",
        {{"room", roomProp()},
         {"title", prop("string", "Short title")},
         {"description", prop("string", "Details, any length")},
         {"priority", prop("string", "low, medium or high")},
         {"claim", prop("boolean", "Claim it for yourself")}},
        {"room", "title"}));
    tools.append(tool("claim_todo",
                      "Claim a todo before working on it. Fails if someone else holds it, "
                      "unless force (only take over abandoned work).",
                      {{"id", prop("integer", "Todo id")}, {"force", prop("boolean", "Take it over")}},
                      {"id"}));
    tools.append(tool("release_todo", "Give up your claim on a todo so someone else can take it.",
                      {{"id", prop("integer", "Todo id")},
                       {"force", prop("boolean", "Release someone else's claim")}},
                      {"id"}));
    tools.append(tool("complete_todo",
                      "Mark a todo done, optionally posting a note (what was done, where) to its room.",
                      {{"id", prop("integer", "Todo id")}, {"note", prop("string", "Summary")}},
                      {"id"}));
    return tools;
}

// ── Tool implementations ──

Room roomArg(const Json::Value &args)
{
    const auto &r = args["room"];
    if (r.isIntegral())
        return TodoStore::get().room(r.asInt64());
    if (r.isString())
        return TodoStore::get().findRoom(http::trim(r.asString()));
    throw ArgError("room must be a room name or id");
}

std::string stringArg(const Json::Value &args, const char *key, bool required, size_t max)
{
    const auto &v = args[key];
    if (v.isNull() && !required)
        return "";
    if (!v.isString() || http::trim(v.asString()).empty())
        throw ArgError(std::string(key) + " must be a non-empty string");
    auto s = http::trim(v.asString());
    if (s.size() > max)
        throw ArgError(std::string(key) + " must be at most " + std::to_string(max) + " characters");
    return s;
}

int64_t intArg(const Json::Value &args, const char *key, int64_t fallback)
{
    const auto &v = args[key];
    if (v.isNull())
        return fallback;
    if (!v.isNumeric())
        throw ArgError(std::string(key) + " must be a number");
    return v.asInt64();
}

void needAgent(const std::string &agent)
{
    if (agent.empty())
        throw ArgError(
            "say who you are: pass `agent`, or set the X-Todo-Agent header on the MCP server "
            "(claude mcp add ... --header \"X-Todo-Agent: NAME\")");
}

Json::Value messagesJson(const std::vector<Message> &msgs, int64_t since)
{
    Json::Value out;
    out["messages"] = Json::Value(Json::arrayValue);
    int64_t cursor = since;
    for (const auto &m : msgs)
    {
        out["messages"].append(m.toJson());
        cursor = std::max(cursor, m.seq);
    }
    out["cursor"] = static_cast<Json::Int64>(cursor);
    return out;
}

Json::Value roomTodos(int64_t roomId)
{
    Json::Value arr(Json::arrayValue);
    for (const auto &t : TodoStore::get().list(true))
        if (t.roomId == roomId)
            arr.append(t.toJson());
    return arr;
}

using Done = std::function<void(Json::Value)>;

// Runs a tool; `done` gets the tools/call result (maybe later, for waits).
void callTool(const std::string &name, const Json::Value &args, const std::string &agent, Done done)
{
    auto &store = TodoStore::get();
    if (name == "list_rooms")
    {
        Json::Value arr(Json::arrayValue);
        const auto todos = store.list(true);
        for (const auto &r : store.listRooms())
        {
            auto v = rooms::roomJson(r);
            int open = 0;
            for (const auto &t : todos)
                open += t.roomId == r.id && !t.done;
            v["openTodos"] = open;
            arr.append(v);
        }
        return done(toolJson(arr));
    }
    if (name == "create_room")
    {
        const auto roomName = stringArg(args, "name", true, 60);
        if (auto msg = rooms::checkName(roomName); !msg.empty())
            throw ArgError(msg);
        const auto r = store.addRoom(roomName, stringArg(args, "topic", false, rooms::kMaxTopic));
        if (!agent.empty())
            store.touch(r.id, agent);
        return done(toolJson(rooms::roomJson(store.room(r.id))));
    }
    if (name == "read_room")
    {
        const auto r = roomArg(args);
        store.touch(r.id, agent);
        const size_t limit = std::clamp<int64_t>(intArg(args, "limit", 50), 1, rooms::kMaxLimit);
        const int64_t since = intArg(args, "since", 0);
        Json::Value out = rooms::roomJson(store.room(r.id));
        out["todos"] = roomTodos(r.id);
        auto msgs = messagesJson(store.messages(r.id, since, limit), since);
        out["messages"] = msgs["messages"];
        out["cursor"] = std::max(msgs["cursor"].asInt64(), out["cursor"].asInt64());
        if (!agent.empty())
            out["you"] = agent;
        return done(toolJson(out));
    }
    if (name == "wait_for_messages")
    {
        const auto r = roomArg(args);
        store.touch(r.id, agent);
        if (!args.isMember("since"))
            throw ArgError("since is required: the cursor from your last read");
        const int64_t since = intArg(args, "since", 0);
        double timeout = 60;
        if (args["timeout"].isNumeric())
            timeout = std::clamp(args["timeout"].asDouble(), 0.0, rooms::kMaxWait);
        return RoomHub::get().wait(r.id, since, rooms::kDefaultLimit, timeout,
                                   [since, done](std::vector<Message> msgs) {
                                       auto out = messagesJson(msgs, since);
                                       if (msgs.empty())
                                           out["timedOut"] = true;
                                       done(toolJson(out));
                                   });
    }
    if (name == "post_message")
    {
        needAgent(agent);
        const auto r = roomArg(args);
        std::vector<Attachment> attachments;
        if (auto msg = files::parseList(args["attachments"], attachments); !msg.empty())
            throw ArgError(msg);
        const auto text = stringArg(args, "text", attachments.empty(), rooms::kMaxMessage);
        return done(toolJson(store.post(r.id, agent, text, attachments).toJson()));
    }
    if (name == "upload_file")
    {
        const auto fileName = stringArg(args, "name", true, 200);
        const auto data = drogon::utils::base64Decode(stringArg(args, "data", true, files::kMaxVideoSize / 3 * 4 + 16));
        return done(toolJson(files::store(data, fileName).toJson()));
    }
    if (name == "get_file")
    {
        const auto id = stringArg(args, "id", true, 20);
        const auto a = files::find(id);
        if (!a)
            throw StoreError(404, "no file " + id);
        if (files::isVideo(a->type))
        {
            // Too big to hand over inline, and nothing to look at as text.
            std::string about = "Video " + a->name + " (" + a->type + ", " + std::to_string(a->size) + " bytes";
            if (a->width && a->height)
                about += ", " + std::to_string(a->width) + "x" + std::to_string(a->height);
            if (a->duration > 0)
                about += ", " + std::to_string(static_cast<int>(a->duration + 0.5)) + " s";
            about += "). Download it from /api/files/" + a->id + " on this server.";
            return done(toolText(about));
        }
        std::ifstream in(files::pathOf(id), std::ios::binary);
        const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Json::Value c;
        if (a->type.rfind("image/", 0) == 0)
        {
            c["type"] = "image";
            c["data"] = drogon::utils::base64Encode(data);
            c["mimeType"] = a->type;
        }
        else
        {
            c["type"] = "resource";
            c["resource"]["uri"] = "todo-file:" + a->id;
            c["resource"]["mimeType"] = a->type;
            c["resource"]["blob"] = drogon::utils::base64Encode(data);
        }
        Json::Value v;
        v["content"].append(c);
        v["isError"] = false;
        return done(v);
    }
    if (name == "add_todo")
    {
        needAgent(agent);
        const auto r = roomArg(args);
        Todo init;
        init.roomId = r.id;
        init.description = stringArg(args, "description", false, 10000);
        if (!args["priority"].isNull())
        {
            init.priority = args["priority"].isString() ? parsePriority(args["priority"].asString()) : -1;
            if (init.priority < 0)
                throw ArgError("priority must be low, medium or high");
        }
        auto t = store.add(stringArg(args, "title", true, 500), init, agent);
        if (args["claim"].asBool())
            t = store.claim(t.id, agent, false);
        return done(toolJson(t.toJson()));
    }
    if (name == "claim_todo" || name == "release_todo")
    {
        needAgent(agent);
        const int64_t id = intArg(args, "id", 0);
        const bool force = args["force"].asBool();
        return done(toolJson((name == "claim_todo" ? store.claim(id, agent, force)
                                                   : store.release(id, agent, force))
                                 .toJson()));
    }
    if (name == "complete_todo")
    {
        needAgent(agent);
        const int64_t id = intArg(args, "id", 0);
        const auto note = stringArg(args, "note", false, rooms::kMaxMessage);
        TodoPatch patch;
        patch.done = true;
        auto t = store.update(id, patch, agent);
        if (!t)
            throw StoreError(404, "todo not found");
        if (!note.empty() && t->roomId)
            store.post(t->roomId, agent, "#" + std::to_string(id) + ": " + note);
        return done(toolJson(t->toJson()));
    }
    throw ArgError("unknown tool " + name);
}

// Handles one JSON-RPC request; `reply` gets the response (null for notifications).
void dispatch(const Json::Value &msg, const std::string &agent, std::function<void(Json::Value)> reply)
{
    const Json::Value id = msg["id"];
    const std::string method = msg["method"].asString();
    const Json::Value &params = msg["params"];

    if (!msg.isMember("id"))  // a notification (initialized, cancelled...)
        return reply(Json::Value());

    if (method == "initialize")
    {
        std::string version = kProtocolVersions[0];
        for (auto v : kProtocolVersions)
            if (params["protocolVersion"].asString() == v)
                version = v;
        Json::Value r;
        r["protocolVersion"] = version;
        r["capabilities"]["tools"] = Json::Value(Json::objectValue);
        r["serverInfo"]["name"] = "todo-rooms";
        r["serverInfo"]["version"] = "1.0.0";
        r["instructions"] = kInstructions;
        return reply(rpcResult(id, r));
    }
    if (method == "ping")
        return reply(rpcResult(id, Json::Value(Json::objectValue)));
    if (method == "tools/list")
    {
        Json::Value r;
        r["tools"] = toolList();
        return reply(rpcResult(id, r));
    }
    if (method == "tools/call")
    {
        const auto name = params["name"].asString();
        const Json::Value args =
            params["arguments"].isObject() ? params["arguments"] : Json::Value(Json::objectValue);
        std::string who = agent;
        if (args["agent"].isString() && !http::trim(args["agent"].asString()).empty())
            who = http::trim(args["agent"].asString()).substr(0, 40);
        try
        {
            callTool(name, args, who, [id, reply](Json::Value result) {
                reply(rpcResult(id, result));
            });
        }
        catch (const StoreError &e)
        {
            reply(rpcResult(id, toolText(e.what(), true)));
        }
        catch (const ArgError &e)
        {
            reply(rpcResult(id, toolText(e.what(), true)));
        }
        return;
    }
    reply(rpcError(id, -32601, "method not found: " + method));
}
}  // namespace

// POST /mcp  -- one JSON-RPC message per request
void McpController::handle(const HttpRequestPtr &req, Callback &&cb)
{
    auto body = req->getJsonObject();
    if (!body || !body->isObject())
        return cb(json(rpcError(Json::Value(), -32700, "expected one JSON-RPC message object"),
                       k400BadRequest));
    std::string err;
    const auto agent = http::agent(req, nullptr, "agent", err);
    dispatch(*body, agent, [cb = std::move(cb)](Json::Value response) {
        if (response.isNull())
            return cb(HttpResponse::newHttpResponse(k202Accepted, CT_NONE));
        cb(json(response));
    });
}

// GET/DELETE /mcp  -- no server-initiated stream and no sessions to end
void McpController::noStream(const HttpRequestPtr &, Callback &&cb)
{
    auto resp = HttpResponse::newHttpResponse(k405MethodNotAllowed, CT_NONE);
    resp->addHeader("Allow", "POST");
    cb(resp);
}
