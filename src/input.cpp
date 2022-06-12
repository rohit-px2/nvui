#include "input.hpp"
#include <fmt/format.h>
#include <fmt/core.h>
#include <unordered_map>
#include <string>
#include <string_view>
#include <QEvent>
#include <QKeyEvent>
#include <QString>

std::string event_to_string(const QKeyEvent* event, bool* special)
{
  *special = true;
  switch(event->key())
  {
    case Qt::Key_Enter:
      return "CR";
    case Qt::Key_Return:
      return "CR";
    case Qt::Key_Backspace:
      return "BS";
    case Qt::Key_Tab:
      return "Tab";
    case Qt::Key_Backtab:
      return "Tab";
    case Qt::Key_Down:
      return "Down";
    case Qt::Key_Up:
      return "Up";
    case Qt::Key_Left:
      return "Left";
    case Qt::Key_Right:
      return "Right";
    case Qt::Key_Escape:
      return "Esc";
    case Qt::Key_Home:
      return "Home";
    case Qt::Key_End:
      return "End";
    case Qt::Key_Insert:
      return "Insert";
    case Qt::Key_Delete:
      return "Del";
    case Qt::Key_PageUp:
      return "PageUp";
    case Qt::Key_PageDown:
      return "PageDown";
    case Qt::Key_Less:
      return "LT";
    case Qt::Key_Space:
      return "Space";
    case Qt::Key_F1:
      return "F1";
    case Qt::Key_F2:
      return "F2";
    case Qt::Key_F3:
      return "F3";
    case Qt::Key_F4:
      return "F4";
    case Qt::Key_F5:
      return "F5";
    case Qt::Key_F6:
      return "F6";
    case Qt::Key_F7:
      return "F7";
    case Qt::Key_F8:
      return "F8";
    case Qt::Key_F9:
      return "F9";
    case Qt::Key_F10:
      return "F10";
    case Qt::Key_F11:
      return "F11";
    case Qt::Key_F12:
      return "F12";
    case Qt::Key_F13:
      return "F13";
    case Qt::Key_F14:
      return "F14";
    case Qt::Key_F15:
      return "F15";
    case Qt::Key_F16:
      return "F16";
    case Qt::Key_F17:
      return "F17";
    case Qt::Key_F18:
      return "F18";
    case Qt::Key_F19:
      return "F19";
    case Qt::Key_F20:
      return "F20";
    default:
      *special = false;
      return event->text().toStdString();
  }
}

[[maybe_unused]]
static bool is_modifier(int key)
{
  switch(key)
  {
    case Qt::Key_Meta:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Shift:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_CapsLock:
      return true;
    default:
      return false;
  }
  return false;
}

/// Taken from Neovim-Qt's "IsAsciiCharRequiringAlt" function in input.cpp.
[[maybe_unused]]
static bool requires_alt(int key, Qt::KeyboardModifiers mods, QChar c)
{
  // Ignore all key events where Alt is not pressed
  if (!(mods & Qt::AltModifier)) return false;

  // These low-ascii characters may require AltModifier on MacOS
  if ((c == '[' && key != Qt::Key_BracketLeft)
    || (c == ']' && key != Qt::Key_BracketRight)
    || (c == '{' && key != Qt::Key_BraceLeft)
    || (c == '}' && key != Qt::Key_BraceRight)
    || (c == '|' && key != Qt::Key_Bar)
    || (c == '~' && key != Qt::Key_AsciiTilde)
    || (c == '@' && key != Qt::Key_At))
  {
    return true;
  }

  return false;
}

/// Normalizes a key event. Does nothing for non-Mac platforms.
/// Taken from Neovim-Qt's "CreatePlatformNormalizedKeyEvent"
/// function for each platform.
static QKeyEvent normalize_key_event(
  QEvent::Type type,
  int key,
  Qt::KeyboardModifiers mods,
  const QString& text
)
{
#if !defined(Q_OS_MAC)
  return {type, key, mods, text};
#else
  if (text.isEmpty())
  {
    return {type, key, mods, text};
  }

  const QChar& c = text.at(0);
  if ((c.unicode() >= 0x80 && c.isPrint()) || requires_alt(key, mods, c))
  {
    mods &= ~Qt::AltModifier;
  }
  return {type, key, mods, text};
#endif // !defined(Q_OS_MAC)
}

/// Taken from Neovim-Qt's Input::ControlModifier()
static constexpr auto c_mod()
{
#if defined(Q_OS_WIN)
  return Qt::ControlModifier;
#elif defined(Q_OS_MAC)
  return Qt::MetaModifier;
#else
  return Qt::ControlModifier;
#endif
}

/// Taken from NeovimQt's Input::CmdModifier()
static constexpr auto d_mod()
{
#if defined(Q_OS_WIN)
  return Qt::NoModifier;
#elif defined(Q_OS_MAC)
  return Qt::ControlModifier;
#else
  return Qt::MetaModifier;
#endif
}


static std::string mod_prefix(Qt::KeyboardModifiers mods)
{
  std::string result;
  if (mods & Qt::ShiftModifier) result.append("S-");
  if (mods & c_mod()) result.append("C-");
  if (mods & Qt::AltModifier) result.append("M-");
  if (mods & d_mod()) result.append("D-");
  return result;
}

/// Equivalent of Neovim's "ToKeyString",
/// but returning an std::string.
static std::string key_mod_str(
  Qt::KeyboardModifiers mods,
  std::string_view text
)
{
  return fmt::format("<{}{}>", mod_prefix(mods), text);
}

/// Derived from Neovim-Qt's NeovimQt::Input::convertKey
/// method, see
/// https://github.com/equalsraf/neovim-qt/blob/master/src/gui/input.cpp#L144
static std::string convertKey(const QKeyEvent& ev) noexcept
{
  bool x = requires_alt(0, Qt::NoModifier, 0);
  Q_UNUSED(x);
  QString text = ev.text();
  auto mod = ev.modifiers();
  int key = ev.key();
  static const std::unordered_map<int, std::string_view> keypadKeys {
    { Qt::Key_Home, "kHome" },
    { Qt::Key_End, "kEnd" },
    { Qt::Key_PageUp, "kPageUp" },
    { Qt::Key_PageDown, "kPageDown" },
    { Qt::Key_Plus, "kPlus" },
    { Qt::Key_Minus, "kMinus" },
    { Qt::Key_multiply, "kMultiply" },
    { Qt::Key_division, "kDivide" },
    { Qt::Key_Enter, "kEnter" },
    { Qt::Key_Period, "kPoint" },
    { Qt::Key_0, "k0" },
    { Qt::Key_1, "k1" },
    { Qt::Key_2, "k2" },
    { Qt::Key_3, "k3" },
    { Qt::Key_4, "k4" },
    { Qt::Key_5, "k5" },
    { Qt::Key_6, "k6" },
    { Qt::Key_7, "k7" },
    { Qt::Key_8, "k8" },
    { Qt::Key_9, "k9" },
  };

#ifdef Q_OS_WIN
  /// Windows sends Ctrl+Alt when AltGr is pressed,
  /// but the text already factors in AltGr. Solution: Ignore Ctrl and Alt
  if ((mod & Qt::ControlModifier) && (mod & Qt::AltModifier))
  {
    mod &= ~Qt::ControlModifier;
    mod &= ~Qt::AltModifier;
  }
#endif // Q_OS_WIN

  if (mod & Qt::KeypadModifier && keypadKeys.contains(key))
  {
    return fmt::format("<{}{}>", mod_prefix(mod), keypadKeys.at(key));
  }

  // Issue#917: On Linux, Control + Space sends text as "\u0000"
  if (key == Qt::Key_Space && text.size() > 0 && !text.at(0).isPrint())
  {
    text = " ";
  }

  bool is_special = false;
  auto s = event_to_string(&ev, &is_special);
  if (is_special)
  {
    if (key == Qt::Key_Space
      || key == Qt::Key_Backspace)
    {
      mod &= ~Qt::ShiftModifier;
    }
    return key_mod_str(mod, s);
  }

  // Issue#864: Some international layouts insert accents (~^`) on Key_Space
  if (key == Qt::Key_Space && !text.isEmpty() && text != " ")
  {
    if (mod != Qt::NoModifier)
    {
      return key_mod_str(mod, text.toStdString());
    }

    return text.toStdString();
  }


  // The key "<" should be sent as "<lt>"
  //   Issue#607: Remove ShiftModifier from "<", shift is implied
  if (text == "<")
  {
    const Qt::KeyboardModifiers modNoShift = mod & ~Qt::KeyboardModifier::ShiftModifier;
    return key_mod_str(modNoShift, "LT");
  }

  // Issue#170: Normalize modifiers, CTRL+^ always sends as <C-^>
  const bool isCaretKey = (key == Qt::Key_6 || key == Qt::Key_AsciiCircum);
  if (isCaretKey && mod & c_mod())
  {
    const Qt::KeyboardModifiers modNoShiftMeta = mod
      & Qt::KeyboardModifier::ShiftModifier
      & ~d_mod();
    return key_mod_str(modNoShiftMeta, "C-^");
  }

  if (text == "\\")
  {
    return key_mod_str(mod, "Bslash");
  }

  if (text.isEmpty())
  {
    // Ignore all modifier-only key events.
    //   Issue#344: Ignore Ctrl-Shift, C-S- being treated as C-Space
    //   Issue#593: Pressing Control + Super inserts ^S
    //   Issue#199: Pressing Control + CapsLock inserts $
    if (is_modifier(key)) return {};

    // Ignore special keys
    //   Issue#671: `q`/`p`/`r` key is sent by Mute/Volume DOWN/Volume UP
    if (key == Qt::Key::Key_VolumeDown
      || key == Qt::Key::Key_VolumeMute
      || key == Qt::Key::Key_VolumeUp) return {};

    text = QChar(key);
    if (!(mod & Qt::ShiftModifier)) text = text.toLower();
  }

  const QChar c = text.at(0);

  // Remove Shift, skip when ALT or CTRL are pressed
  if ((c.unicode() >= 0x80 || c.isPrint())
    && !(mod & c_mod()) && !(mod & d_mod()))
  {
    mod &= ~Qt::ShiftModifier;
  }

  // Ignore empty characters at the start of the ASCII range
  if (c.unicode() < 0x20)
  {
    text = QChar(key);
    if (!(mod & Qt::ShiftModifier)) text = text.toLower();
  }

  // Perform any platform specific QKeyEvent modifications
  auto evNormalized = normalize_key_event(ev.type(), key, mod, text);

  const auto prefix = mod_prefix(evNormalized.modifiers());
  if (!prefix.empty())
  {
    auto normalized_mods = evNormalized.modifiers();
    return key_mod_str(normalized_mods, evNormalized.text().toStdString());
  }

  return evNormalized.text().toStdString();
}

std::string convert_key(const QKeyEvent& ev)
{
  return convertKey(ev);
}
