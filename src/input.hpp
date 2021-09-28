#ifndef NVUI_INPUT_HPP
#define NVUI_INPUT_HPP

#include <QEvent>
#include <QKeyEvent>
#include <QString>
#include <string>

/// Converts a key event to Neovim input string.
/// Most of this code is taken from Neovim-Qt,
/// see 
/// https://github.com/equalsraf/neovim-qt/blob/master/src/gui/input.hpp
/// and
/// https://github.com/equalsraf/neovim-qt/blob/master/src/gui/input.cpp
/// Changed a little bit to return an std::string instead of a QString
std::string convert_key(const QKeyEvent& ev);

#endif // NVUI_INPUT_HPP
