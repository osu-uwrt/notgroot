// notgroot [trees directory or tree file] [--run FILE.btlog] [--theme ID] [--screenshot FILE --frames N] [--no-persist]
#include "app.hpp"
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    notgroot::app::Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "notgroot: " << arg << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--theme")
            options.theme = value();
        else if (arg == "--screenshot")
            options.screenshot = value();
        else if (arg == "--frames") {
            try {
                options.frames = std::stoi(value());
            } catch (const std::exception &) {
                std::cerr << "notgroot: --frames needs a number\n";
                return 2;
            }
        } else if (arg == "--keys")
            options.keys = value();
        else if (arg == "--type")
            options.text = value();
        else if (arg == "--run")
            options.run = value();
        else if (arg == "--no-persist")
            options.persist = false;
        else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: notgroot [trees directory or tree file] [--run FILE.btlog] [--theme ID] "
                         "[--screenshot FILE --frames N] [--no-persist]\n";
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "notgroot: unknown option " << arg << '\n';
            return 2;
        } else
            options.open = arg;
    }
    if (!options.screenshot.empty() && options.frames <= 0)
        options.frames = 3;

    try {
        return notgroot::app::App(options).run();
    } catch (const std::exception &error) {
        std::cerr << "notgroot: " << error.what() << '\n';
        return 1;
    }
}
