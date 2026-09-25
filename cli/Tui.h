#pragma once

class Api;

// Runs the full-screen interactive UI until the user quits. Returns exit code.
int runTui(Api &api);
