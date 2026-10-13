#include "CategoryController.h"
#include "HttpUtil.h"
#include "TodoStore.h"

using http::error;
using http::json;

namespace
{
constexpr size_t kMaxName = 60;

// Validates a category name; returns an error message, or "" if it's fine.
std::string checkName(const std::string &name)
{
    if (name.empty() || name.size() > kMaxName)
        return "name must be 1-60 characters";
    if (name.find('/') != std::string::npos)
        return "name can't contain \"/\"";
    return "";
}
}  // namespace

// GET /api/categories[?archived=1]
void CategoryController::list(const HttpRequestPtr &req, Callback &&cb)
{
    Json::Value arr(Json::arrayValue);
    for (const auto &c : TodoStore::get().listCategories(http::wantsArchived(req)))
        arr.append(c.toJson());
    cb(json(arr));
}

// POST /api/categories  {"name": "...", "parentId"?: id|null}
void CategoryController::create(const HttpRequestPtr &req, Callback &&cb)
{
    auto body = req->getJsonObject();
    if (!body || !(*body)["name"].isString())
        return cb(error(400, "body must be JSON with a \"name\" string"));

    auto name = http::trim((*body)["name"].asString());
    if (auto msg = checkName(name); !msg.empty())
        return cb(error(400, msg));

    std::optional<int64_t> parentId;
    if (!http::parseRef(*body, "parentId", parentId))
        return cb(error(400, "parentId must be an id or null"));

    try
    {
        auto c = TodoStore::get().addCategory(name, parentId.value_or(0));
        cb(json(c.toJson(), k201Created));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// PATCH /api/categories/{id}
//   {"name"?: "...", "parentId"?: id|null, "archived"?: bool,
//    "style"?: {"fg"?, "bg"?, "bold"?, "italic"?, "underline"?}|null}
void CategoryController::update(const HttpRequestPtr &req, Callback &&cb, int64_t id)
{
    auto body = req->getJsonObject();
    if (!body || !body->isObject())
        return cb(error(400, "body must be a JSON object"));

    std::optional<std::string> name;
    std::optional<int64_t> parentId;
    std::optional<bool> archived;
    std::optional<CategoryStyle> style;
    if (body->isMember("name"))
    {
        if (!(*body)["name"].isString())
            return cb(error(400, "name must be a string"));
        name = http::trim((*body)["name"].asString());
        if (auto msg = checkName(*name); !msg.empty())
            return cb(error(400, msg));
    }
    if (!http::parseRef(*body, "parentId", parentId))
        return cb(error(400, "parentId must be an id or null"));
    if (body->isMember("archived"))
    {
        if (!(*body)["archived"].isBool())
            return cb(error(400, "archived must be a boolean"));
        archived = (*body)["archived"].asBool();
    }
    if (body->isMember("style"))
    {
        style.emplace();
        if (auto msg = CategoryStyle::fromJson((*body)["style"], *style); !msg.empty())
            return cb(error(400, msg));
    }
    if (!name && !parentId && !archived && !style)
        return cb(error(400, "nothing to update"));

    try
    {
        cb(json(TodoStore::get().updateCategory(id, name, parentId, archived, style).toJson()));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}

// DELETE /api/categories/{id}  -- todos and subcategories move to the parent
void CategoryController::remove(const HttpRequestPtr &, Callback &&cb, int64_t id)
{
    try
    {
        TodoStore::get().removeCategory(id);
        cb(HttpResponse::newHttpResponse(k204NoContent, CT_NONE));
    }
    catch (const StoreError &e)
    {
        cb(error(e));
    }
}
