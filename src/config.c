/*
 * vim:ts=4:sw=4:expandtab
 *
 * i3 - an improved tiling window manager
 * © 2009 Michael Stapelberg and contributors (see also: LICENSE)
 *
 * config.c: Configuration file (calling the parser (src/config_parser.c) with
 *           the correct path, switching key bindings mode).
 *
 */
#include "all.h"

#include <libgen.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>

char *current_configpath = NULL;
Config config;
struct modes_head modes;
struct barconfig_head barconfigs = TAILQ_HEAD_INITIALIZER(barconfigs);
struct includedfiles_head included_files = TAILQ_HEAD_INITIALIZER(included_files);

/*
 * Ungrabs all keys, to be called before re-grabbing the keys because of a
 * mapping_notify event or a configuration file reload
 *
 */
void ungrab_all_keys(xcb_connection_t *conn) {
    DLOG("Ungrabbing all keys\n");
    xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_BUTTON_MASK_ANY);
}

static void free_configuration(void) {
    assert(conn != NULL);

    /* If we are currently in a binding mode, we first revert to the default
     * since we have no guarantee that the current mode will even still exist
     * after parsing the config again. See #2228. */
    switch_mode("default");

    /* First ungrab the keys */
    ungrab_all_keys(conn);

    struct Mode *mode;
    while (!SLIST_EMPTY(&modes)) {
        mode = SLIST_FIRST(&modes);
        FREE(mode->name);

        /* Clear the old binding list */
        while (!TAILQ_EMPTY(mode->bindings)) {
            Binding *bind = TAILQ_FIRST(mode->bindings);
            TAILQ_REMOVE(mode->bindings, bind, bindings);
            binding_free(bind);
        }
        FREE(mode->bindings);

        SLIST_REMOVE(&modes, mode, Mode, modes);
        FREE(mode);
    }

    while (!TAILQ_EMPTY(&assignments)) {
        struct Assignment *assign = TAILQ_FIRST(&assignments);
        if (assign->type == A_TO_WORKSPACE || assign->type == A_TO_WORKSPACE_NUMBER) {
            FREE(assign->dest.workspace);
        } else if (assign->type == A_COMMAND) {
            FREE(assign->dest.command);
        } else if (assign->type == A_TO_OUTPUT) {
            FREE(assign->dest.output);
        }
        match_free(&(assign->match));
        TAILQ_REMOVE(&assignments, assign, assignments);
        FREE(assign);
    }

    while (!TAILQ_EMPTY(&ws_assignments)) {
        struct Workspace_Assignment *assign = TAILQ_FIRST(&ws_assignments);
        FREE(assign->name);
        FREE(assign->output);
        TAILQ_REMOVE(&ws_assignments, assign, ws_assignments);
        FREE(assign);
    }

    /* Clear bar configs */
    Barconfig *barconfig;
    while (!TAILQ_EMPTY(&barconfigs)) {
        barconfig = TAILQ_FIRST(&barconfigs);
        FREE(barconfig->id);
        for (int c = 0; c < barconfig->num_outputs; c++) {
            free(barconfig->outputs[c]);
        }

        while (!TAILQ_EMPTY(&(barconfig->bar_bindings))) {
            struct Barbinding *binding = TAILQ_FIRST(&(barconfig->bar_bindings));
            FREE(binding->command);
            TAILQ_REMOVE(&(barconfig->bar_bindings), binding, bindings);
            FREE(binding);
        }

        while (!TAILQ_EMPTY(&(barconfig->tray_outputs))) {
            struct tray_output_t *tray_output = TAILQ_FIRST(&(barconfig->tray_outputs));
            FREE(tray_output->output);
            TAILQ_REMOVE(&(barconfig->tray_outputs), tray_output, tray_outputs);
            FREE(tray_output);
        }

        FREE(barconfig->outputs);
        FREE(barconfig->socket_path);
        FREE(barconfig->status_command);
        FREE(barconfig->workspace_command);
        FREE(barconfig->i3bar_command);
        FREE(barconfig->font);
        FREE(barconfig->colors.background);
        FREE(barconfig->colors.statusline);
        FREE(barconfig->colors.separator);
        FREE(barconfig->colors.focused_background);
        FREE(barconfig->colors.focused_statusline);
        FREE(barconfig->colors.focused_separator);
        FREE(barconfig->colors.focused_workspace_border);
        FREE(barconfig->colors.focused_workspace_bg);
        FREE(barconfig->colors.focused_workspace_text);
        FREE(barconfig->colors.active_workspace_border);
        FREE(barconfig->colors.active_workspace_bg);
        FREE(barconfig->colors.active_workspace_text);
        FREE(barconfig->colors.inactive_workspace_border);
        FREE(barconfig->colors.inactive_workspace_bg);
        FREE(barconfig->colors.inactive_workspace_text);
        FREE(barconfig->colors.urgent_workspace_border);
        FREE(barconfig->colors.urgent_workspace_bg);
        FREE(barconfig->colors.urgent_workspace_text);
        FREE(barconfig->colors.binding_mode_border);
        FREE(barconfig->colors.binding_mode_bg);
        FREE(barconfig->colors.binding_mode_text);
        TAILQ_REMOVE(&barconfigs, barconfig, configs);
        FREE(barconfig);
    }

    Con *con;
    TAILQ_FOREACH (con, &all_cons, all_cons) {
        /* Assignments changed, previously ran assignments are invalid. */
        if (con->window) {
            con->window->nr_assignments = 0;
            FREE(con->window->ran_assignments);
        }
        /* Invalidate pixmap caches in case font or colors changed. */
        FREE(con->deco_render_params);
    }

    /* Get rid of the current font */
    free_font();

    free(config.ipc_socket_path);
    free(config.restart_state_path);
    free(config.fake_outputs);
}

/*
 * (Re-)loads the configuration file (sets useful defaults before).
 *
 * If you specify override_configpath, only this path is used to look for a
 * configuration file.
 *
 * load_type specifies the type of loading: C_VALIDATE is used to only verify
 * the correctness of the config file (used with the flag -C). C_LOAD will load
 * the config for normal use and display errors in the nagbar. C_RELOAD will
 * also clear the previous config.
 *
 */
bool load_configuration(const char *override_configpath, config_load_t load_type) {
    if (load_type == C_RELOAD) {
        free_configuration();
    }

    SLIST_INIT(&modes);

    struct Mode *default_mode = scalloc(1, sizeof(struct Mode));
    default_mode->name = sstrdup("default");
    default_mode->bindings = scalloc(1, sizeof(struct bindings_head));
    TAILQ_INIT(default_mode->bindings);
    SLIST_INSERT_HEAD(&modes, default_mode, modes);

    bindings = default_mode->bindings;
    current_binding_mode = default_mode->name;

    /* Clear the old config or initialize the data structure */
    memset(&config, 0, sizeof(config));

    /* Initialize default colors */
#define INIT_COLOR(x, cborder, cbackground, ctext, cindicator) \
    do {                                                       \
        x.border = draw_util_hex_to_color(cborder);            \
        x.background = draw_util_hex_to_color(cbackground);    \
        x.text = draw_util_hex_to_color(ctext);                \
        x.indicator = draw_util_hex_to_color(cindicator);      \
        x.child_border = draw_util_hex_to_color(cbackground);  \
    } while (0)

    config.client.background = draw_util_hex_to_color("#000000");
    config.client.gradient_start = draw_util_hex_to_color("#1f1947");
    config.client.gradient_end = draw_util_hex_to_color("#2e9ef4");
    config.client.gradient_unfocused_start = draw_util_hex_to_color("#303331");
    config.client.gradient_unfocused_end = draw_util_hex_to_color("#9da6a0");
    config.client.dither_noise = 0.5;
    config.client.gradients = 1;
    config.client.gradient_offset_start = 0.0;
    config.client.gradient_offset_end = 1.0;
    INIT_COLOR(config.client.focused, "#4c7899", "#285577", "#ffffff", "#2e9ef4");
    INIT_COLOR(config.client.focused_inactive, "#333333", "#5f676a", "#ffffff", "#484e50");
    INIT_COLOR(config.client.unfocused, "#333333", "#222222", "#888888", "#292d2e");
    INIT_COLOR(config.client.urgent, "#2f343a", "#900000", "#ffffff", "#900000");
    config.client.got_focused_tab_title = false;

    /* border and indicator color are ignored for placeholder contents */
    INIT_COLOR(config.client.placeholder, "#000000", "#0c0c0c", "#ffffff", "#000000");

    /* the last argument (indicator color) is ignored for bar colors */
    INIT_COLOR(config.bar.focused, "#4c7899", "#285577", "#ffffff", "#000000");
    INIT_COLOR(config.bar.unfocused, "#333333", "#222222", "#888888", "#000000");
    INIT_COLOR(config.bar.urgent, "#2f343a", "#900000", "#ffffff", "#000000");

    config.show_marks = true;

    config.default_border = BS_NORMAL;
    config.default_floating_border = BS_NORMAL;
    config.default_border_width = logical_px(2);
    config.default_floating_border_width = logical_px(2);
    /* Set default_orientation to NO_ORIENTATION for auto orientation. */
    config.default_orientation = NO_ORIENTATION;

    config.gaps.inner = 0;
    config.gaps.top = 0;
    config.gaps.right = 0;
    config.gaps.bottom = 0;
    config.gaps.left = 0;

    /* Set default urgency reset delay to 500ms */
    if (config.workspace_urgency_timer == 0) {
        config.workspace_urgency_timer = 0.5;
    }

    config.focus_wrapping = FOCUS_WRAPPING_ON;

    config.tiling_drag = TILING_DRAG_MODIFIER;
    config.swap_modifier = XCB_KEY_BUT_MASK_SHIFT;

    FREE(current_configpath);
    current_configpath = get_config_path(override_configpath, true);
    if (current_configpath == NULL) {
        die("Unable to find the configuration file (looked at "
            "$XDG_CONFIG_HOME/i3-gradients/config, ~/.i3-gradients/config, $XDG_CONFIG_DIRS/i3-gradients/config "
            "and " SYSCONFDIR "/i3-gradients/config)");
    }

    IncludedFile *file;
    while (!TAILQ_EMPTY(&included_files)) {
        file = TAILQ_FIRST(&included_files);
        FREE(file->path);
        FREE(file->raw_contents);
        FREE(file->variable_replaced_contents);
        TAILQ_REMOVE(&included_files, file, files);
        FREE(file);
    }

    char resolved_path[PATH_MAX] = {'\0'};
    if (realpath(current_configpath, resolved_path) == NULL) {
        die("realpath(%s): %s", current_configpath, strerror(errno));
    }

    file = scalloc(1, sizeof(IncludedFile));
    file->path = sstrdup(resolved_path);
    TAILQ_INSERT_TAIL(&included_files, file, files);

    LOG("Parsing configfile %s\n", resolved_path);
    struct stack stack;
    memset(&stack, '\0', sizeof(struct stack));
    struct parser_ctx ctx = {
        .use_nagbar = (load_type != C_VALIDATE),
        .stack = &stack,
    };
    SLIST_INIT(&(ctx.variables));
    const int result = parse_file(&ctx, resolved_path, file);
    free_variables(&ctx);
    if (result == -1) {
        die("Could not open configuration file: %s\n", strerror(errno));
    }

    extract_workspace_names_from_bindings();
    reorder_bindings();

    if (config.font.type == FONT_TYPE_NONE && load_type != C_VALIDATE) {
        ELOG("You did not specify required configuration option \"font\"\n");
        config.font = load_font("fixed", true);
        set_font(&config.font);
    }

    /* Make bar config blocks without a configured font use the i3-wide font. */
    Barconfig *current;
    if (load_type != C_VALIDATE) {
        TAILQ_FOREACH (current, &barconfigs, configs) {
            if (current->font != NULL) {
                continue;
            }
            current->font = sstrdup(config.font.pattern);
        }
    }

    if (load_type == C_RELOAD) {
        translate_keysyms();
        grab_all_keys(conn);
        regrab_all_buttons(conn);
        gaps_reapply_workspace_assignments();

        /* Redraw the currently visible decorations on reload, so that the
         * possibly new drawing parameters changed. */
        tree_render();
    }

    return result == 0;
}
