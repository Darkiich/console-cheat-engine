#pragma once
#include <string>

#include "core/Process.h"

// Почему вышли: сам или игра закрылась.
enum class MenuExit { User, ProcessExited };

// Собственно само меню.
MenuExit runMenu(const Process& process, const std::string& saveFile);
