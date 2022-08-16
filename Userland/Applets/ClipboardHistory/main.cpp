/*
 * Copyright (c) 2019-2020, Sergey Bugaev <bugaevc@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ClipboardHistoryModel.h"
#include <LibConfig/Client.h>
#include <LibCore/System.h>
#include <LibGUI/Action.h>
#include <LibGUI/Application.h>
#include <LibGUI/ImageWidget.h>
#include <LibGUI/Menu.h>
#include <LibGUI/TableView.h>
#include <LibGUI/Window.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    TRY(Core::System::pledge("stdio recvfd sendfd rpath unix"));
    auto app = TRY(GUI::Application::try_create(arguments));

    Config::pledge_domain("ClipboardHistory");
    Config::monitor_domain("ClipboardHistory");

    TRY(Core::System::pledge("stdio recvfd sendfd rpath"));
    TRY(Core::System::unveil("/res", "r"));
    TRY(Core::System::unveil("/home/anon/.clipboard", "r"));
    TRY(Core::System::unveil(nullptr, nullptr));
    auto app_icon = TRY(GUI::Icon::try_create_default_icon("edit-copy"sv));

    auto main_window = TRY(GUI::Window::try_create());
    main_window->set_title("Clipboard history");
    main_window->set_rect(670, 65, 325, 500);
    main_window->set_icon(app_icon.bitmap_for_size(16));

    auto table_view = TRY(main_window->try_set_main_widget<GUI::TableView>());
    auto model = ClipboardHistoryModel::create();
    table_view->set_model(model);

    // Load already existing data
    auto clipboard_file = Core::File::open("/home/anon/.clipboard", Core::OpenMode::ReadOnly).value();
    auto file_contents = clipboard_file->read_all();
    auto json_or_error = JsonValue::from_string(file_contents);
    auto json = json_or_error.release_value();

    if (json_or_error.is_error()) {
        dbgln("Failed to parse /home/anon/.clipboard");
    } else {
        json.as_array().for_each([](JsonValue const& object) {
            dbgln("Adding  --> {}", object);
            auto data = object.as_object().get("Data"sv).to_string().bytes();
            auto mime_type = object.as_object().get("Type"sv).to_string();
            HashMap<String, String> metadata;
            // FIXME: Add favorite
            GUI::Clipboard::the().set_data(data, mime_type, metadata);
        });
    }

    table_view->on_activation = [&](GUI::ModelIndex const& index) {
        auto& data_and_type = model->item_at(index.row()).data_and_type;
        GUI::Clipboard::the().set_data(data_and_type.data, data_and_type.mime_type, data_and_type.metadata);
    };

    auto delete_action = GUI::CommonActions::make_delete_action([&](const GUI::Action&) {
        model->remove_item(table_view->selection().first().row());
    });

    auto debug_dump_action = GUI::Action::create("Dump to debug console", [&](const GUI::Action&) {
        table_view->selection().for_each_index([&](GUI::ModelIndex& index) {
            dbgln("{}", model->data(index, GUI::ModelRole::Display).as_string());
        });
    });

    auto entry_context_menu = TRY(GUI::Menu::try_create());
    TRY(entry_context_menu->try_add_action(delete_action));
    TRY(entry_context_menu->try_add_action(debug_dump_action));
    table_view->on_context_menu_request = [&](GUI::ModelIndex const&, GUI::ContextMenuEvent const& event) {
        delete_action->set_enabled(!table_view->selection().is_empty());
        debug_dump_action->set_enabled(!table_view->selection().is_empty());
        entry_context_menu->popup(event.screen_position());
    };

    // Saved x and y positions since their values will be 0 if we hide the window
    int saved_x = main_window->x();
    int saved_y = main_window->y();

    auto applet_window = TRY(GUI::Window::try_create());
    applet_window->set_title("ClipboardHistory");
    applet_window->set_window_type(GUI::WindowType::Applet);
    applet_window->set_has_alpha_channel(true);
    auto icon_widget = TRY(applet_window->try_set_main_widget<GUI::ImageWidget>());
    icon_widget->set_tooltip("Clipboard History");
    icon_widget->load_from_file("/res/icons/16x16/edit-copy.png"sv);
    icon_widget->on_click = [&main_window = *main_window, &saved_x, &saved_y] {
        if (main_window.is_visible()) {
            if (main_window.is_active()) {
                saved_x = main_window.x();
                saved_y = main_window.y();
                main_window.hide();
            } else {
                main_window.move_to_front();
            }
        } else {
            main_window.set_rect(saved_x, saved_y, main_window.width(), main_window.height());
            main_window.show();
            main_window.move_to_front();
        }
    };
    applet_window->resize(16, 16);
    applet_window->show();

    return app->exec();
}
