#include <cassert>
#include <iostream>

#include "session.h"

using json = nlohmann::json;

static SessionFile make_sample_session() {
    SessionFile s;
    s.wm = "sway";

    WorkspaceSession ws;
    ws.name = "1";
    ws.output = "eDP-1";

    LayoutNode left;
    left.type = LayoutNode::Type::Window;
    left.window = WindowInfo{"kitty", {"kitty"}, 0, 0, 960, 1080};

    LayoutNode right;
    right.type = LayoutNode::Type::Window;
    right.window = WindowInfo{"firefox", {"firefox"}, 960, 0, 960, 1080};

    LayoutNode root;
    root.type = LayoutNode::Type::SplitH;
    root.children = {left, right};
    ws.layout = root;

    s.workspaces.push_back(ws);
    return s;
}

static void test_json_round_trip() {
    SessionFile original = make_sample_session();
    json j = to_json(original);
    SessionFile parsed = session_from_json(j);

    assert(parsed.wm == "sway");
    assert(parsed.workspaces.size() == 1);
    const auto& ws = parsed.workspaces[0];
    assert(ws.name == "1");
    assert(ws.layout.has_value());
    assert(ws.layout->type == LayoutNode::Type::SplitH);
    assert(ws.layout->children.size() == 2);
    assert(ws.layout->children[0].window->wm_class == "kitty");
    assert(ws.layout->children[1].window->wm_class == "firefox");
}

static void test_empty_workspace_round_trips_as_null_layout() {
    SessionFile s;
    s.wm = "sway";
    WorkspaceSession ws;
    ws.name = "2";
    ws.output = "eDP-1";
    // ws.layout left as std::nullopt — Section 1 decision 1A
    s.workspaces.push_back(ws);

    json j = to_json(s);
    assert(j["workspaces"][0]["layout"].is_null());

    SessionFile parsed = session_from_json(j);
    assert(!parsed.workspaces[0].layout.has_value());
}

static void test_missing_required_field_throws_schema_error() {
    json j = to_json(make_sample_session());
    j.erase("wm");
    bool threw = false;
    try {
        session_from_json(j);
    } catch (const SchemaValidationError& e) {
        threw = true;
        assert(std::string(e.what()).find("wm") != std::string::npos);
    }
    assert(threw);
}

static void test_wrong_type_field_throws_schema_error() {
    json j = to_json(make_sample_session());
    j["wm"] = 42;  // should be a string
    bool threw = false;
    try {
        session_from_json(j);
    } catch (const SchemaValidationError&) {
        threw = true;
    }
    assert(threw);
}

static void test_unknown_wm_value_throws_schema_error() {
    json j = to_json(make_sample_session());
    j["wm"] = "windows-95";
    bool threw = false;
    try {
        session_from_json(j);
    } catch (const SchemaValidationError&) {
        threw = true;
    }
    assert(threw);
}

static void test_pretty_render_shows_box_drawing_tree() {
    SessionFile s = make_sample_session();
    std::string rendered = render_pretty(s.workspaces[0]);
    assert(rendered.find("split_h") != std::string::npos);
    assert(rendered.find("kitty") != std::string::npos);
    assert(rendered.find("firefox") != std::string::npos);
    assert(rendered.find(u8"├──") != std::string::npos ||  // "├──"
           rendered.find(u8"└──") != std::string::npos);  // "└──"
}

static void test_pretty_render_empty_workspace() {
    SessionFile s;
    s.wm = "sway";
    WorkspaceSession ws;
    ws.name = "3";
    ws.output = "eDP-1";
    std::string rendered = render_pretty(ws);
    assert(rendered.find("(empty)") != std::string::npos);
}

static void test_non_array_floating_or_scratchpad_throws_schema_error() {
    {
        json j = to_json(make_sample_session());
        j["workspaces"][0]["floating"] = "not-an-array";
        bool threw = false;
        try {
            session_from_json(j);
        } catch (const SchemaValidationError& e) {
            threw = true;
            assert(std::string(e.what()).find("floating") != std::string::npos);
        }
        assert(threw);
    }
    {
        json j = to_json(make_sample_session());
        j["workspaces"][0]["scratchpad"] = 123;
        bool threw = false;
        try {
            session_from_json(j);
        } catch (const SchemaValidationError& e) {
            threw = true;
            assert(std::string(e.what()).find("scratchpad") != std::string::npos);
        }
        assert(threw);
    }
}

static void test_non_object_geometry_throws_schema_error() {
    json j = to_json(make_sample_session());
    j["workspaces"][0]["layout"]["children"][0]["geometry"] = "bad";
    bool threw = false;
    try {
        session_from_json(j);
    } catch (const SchemaValidationError&) {
        threw = true;
    }
    assert(threw);
}

static void test_workspace_name_less_ordering_and_overflow() {
    WorkspaceSession w1{"1", "DP-1", std::nullopt, {}, {}};
    WorkspaceSession w2{"2: web", "DP-1", std::nullopt, {}, {}};
    WorkspaceSession w10{"10", "DP-1", std::nullopt, {}, {}};
    WorkspaceSession walpha{"scratch", "DP-1", std::nullopt, {}, {}};
    WorkspaceSession whuge{"99999999999999999999999999", "DP-1", std::nullopt, {}, {}};

    assert(workspace_name_less(w1, w2));
    assert(workspace_name_less(w2, w10));
    assert(workspace_name_less(w10, walpha));
    assert(!workspace_name_less(walpha, w1));
    // Huge numeric prefix overflows long -> treated safely as non-numeric without throwing
    assert(workspace_name_less(w1, whuge));
}

static void test_normalize_proc_cmdline() {
    {
        auto got = normalize_proc_cmdline(
            {"/opt/brave-bin/brave --profile-directory=Default --app-id=abc"},
            "/opt/brave-bin/brave");
        assert((got == std::vector<std::string>{
                           "/opt/brave-bin/brave", "--profile-directory=Default", "--app-id=abc"}));
    }
    {
        auto got = normalize_proc_cmdline({"/opt/My App/bin --flag --arg=1"}, "/opt/My App/bin");
        assert((got == std::vector<std::string>{"/opt/My App/bin", "--flag", "--arg=1"}));
    }
    {
        auto got = normalize_proc_cmdline({"/usr/bin/helium-browser --ozone-platform=wayland"},
                                          "/usr/bin/helium-browser (deleted)");
        assert((got == std::vector<std::string>{"/usr/bin/helium-browser", "--ozone-platform=wayland"}));
    }
    {
        auto got = normalize_proc_cmdline({"/opt/brave-bin/brave --app-id=xyz"}, "");
        assert((got == std::vector<std::string>{"/opt/brave-bin/brave", "--app-id=xyz"}));
    }
    {
        std::vector<std::string> untouched{"echo", "; rm -rf ~"};
        assert(normalize_proc_cmdline(untouched, "/bin/echo") == untouched);
    }
}

int main() {
    test_json_round_trip();
    test_empty_workspace_round_trips_as_null_layout();
    test_missing_required_field_throws_schema_error();
    test_wrong_type_field_throws_schema_error();
    test_unknown_wm_value_throws_schema_error();
    test_pretty_render_shows_box_drawing_tree();
    test_pretty_render_empty_workspace();
    test_non_array_floating_or_scratchpad_throws_schema_error();
    test_non_object_geometry_throws_schema_error();
    test_workspace_name_less_ordering_and_overflow();
    test_normalize_proc_cmdline();
    std::cout << "ok" << std::endl;
    return 0;
}
