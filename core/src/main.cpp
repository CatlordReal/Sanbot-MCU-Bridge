#include "control-catalogue.h"
#include "command-database.h"
#include "usb-send.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace std;

static volatile sig_atomic_t stopRequested = 0;

static void handleSignal(int) {
  stopRequested = 1;
}

static string lowerString(string s) {
  transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return s;
}

static bool parseByteValue(const string &s, uint8_t &out) {
  try {
    int val = stoi(s, nullptr, 0);
    if (val < 0 || val > 255)
      return false;
    out = static_cast<uint8_t>(val);
    return true;
  } catch (...) {
    return false;
  }
}

static bool parseU16Value(const string &s, uint16_t &out) {
  try {
    int val = stoi(s, nullptr, 0);
    if (val < 0 || val > 65535)
      return false;
    out = static_cast<uint16_t>(val);
    return true;
  } catch (...) {
    return false;
  }
}

static void log_packet(const vector<unsigned char> &packet) {
  printf("[VERBOSE] ");
  for (size_t i = 0; i < packet.size(); ++i) {
    printf("%02X", packet[i]);
    if (i + 1 != packet.size())
      printf(" ");
  }
  printf("\n");
  fflush(stdout);
}

static void log_received(uint16_t pid, const vector<unsigned char> &packet) {
  printf("[RECV %04X] ", pid);
  for (size_t i = 0; i < packet.size(); ++i) {
    printf("%02X", packet[i]);
    if (i + 1 != packet.size())
      printf(" ");
  }
  printf("\n");
  fflush(stdout);
}

static bool parseWheelAction(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "forward")
    out = 0x01;
  else if (k == "back")
    out = 0x02;
  else if (k == "left")
    out = 0x03;
  else if (k == "right")
    out = 0x04;
  else if (k == "left-forward")
    out = 0x05;
  else if (k == "right-forward")
    out = 0x06;
  else if (k == "left-back")
    out = 0x07;
  else if (k == "right-back")
    out = 0x08;
  else if (k == "left-translation")
    out = 0x0A;
  else if (k == "right-translation")
    out = 0x0B;
  else if (k == "turn-left")
    out = 0x0C;
  else if (k == "turn-right")
    out = 0x0D;
  else if (k == "stop-turn")
    out = 0xF0;
  else if (k == "stop")
    out = 0x00;
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseArmPart(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "left")
    out = 0x01;
  else if (k == "right")
    out = 0x02;
  else if (k == "both")
    out = 0x03;
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseArmAction(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "up")
    out = 0x01;
  else if (k == "down")
    out = 0x02;
  else if (k == "stop")
    out = 0x03;
  else if (k == "reset")
    out = 0x04;
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseHeadAction(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "stop")
    out = 0x00;
  else if (k == "up")
    out = 0x01;
  else if (k == "down")
    out = 0x02;
  else if (k == "left")
    out = 0x03;
  else if (k == "right")
    out = 0x04;
  else if (k == "left-up")
    out = 0x05;
  else if (k == "right-up")
    out = 0x06;
  else if (k == "left-down")
    out = 0x07;
  else if (k == "right-down")
    out = 0x08;
  else if (k == "vertical-reset")
    out = 0x09;
  else if (k == "horizontal-reset")
    out = 0x0A;
  else if (k == "centre-reset")
    out = 0x0B;
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseHeadAbsoluteAction(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "vertical")
    out = 0x01;
  else if (k == "horizontal")
    out = 0x02;
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseHeadLockAction(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "no-lock")
    out = 0x00;
  else if (k == "horizontal-lock")
    out = 0x01;
  else if (k == "vertical-lock")
    out = 0x02;
  else if (k == "both-lock")
    out = 0x03;
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseHeadDirection(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "left")
    out = 0x01;
  else if (k == "right")
    out = 0x02;
  else if (k == "up")
    out = 0x01;
  else if (k == "down")
    out = 0x02;
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseOnOff(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "on" || k == "enable" || k == "enabled" || k == "true" ||
      k == "1")
    out = 0x01;
  else if (k == "off" || k == "disable" || k == "disabled" ||
           k == "false" || k == "0")
    out = 0x00;
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseLedTarget(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k == "all" || k == "broadcast")
    out = 0x00;
  else if (k == "wheel")
    out = 0x01;
  else if (k == "left-hand" || k == "left-arm" || k == "arm-left")
    out = 0x02;
  else if (k == "right-hand" || k == "right-arm" || k == "arm-right")
    out = 0x03;
  else if (k == "left-head" || k == "left-ear" || k == "ear-left")
    out = 0x04;
  else if (k == "head-left")
    out = 0x04;
  else if (k == "right-head" || k == "right-ear" || k == "ear-right")
    out = 0x05;
  else if (k == "head-right")
    out = 0x05;
  else if (k == "head" || k == "head-all")
    out = 0x0A;
  else if (k.rfind("led-", 0) == 0)
    return parseByteValue(k.substr(4), out);
  else if (k.rfind("led", 0) == 0 && k.size() > 3)
    return parseByteValue(k.substr(3), out);
  else
    return parseByteValue(s, out);
  return true;
}

static bool parseFaceMode(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (k.rfind("face-", 0) == 0)
    return parseByteValue(k.substr(5), out);
  if (k.rfind("face", 0) == 0 && k.size() > 4)
    return parseByteValue(k.substr(4), out);
  if (k.rfind("expression-", 0) == 0)
    return parseByteValue(k.substr(11), out);
  if (k.rfind("expression", 0) == 0 && k.size() > 10)
    return parseByteValue(k.substr(10), out);
  return parseByteValue(s, out);
}

static bool parseLedMode(const string &s, uint8_t &out) {
  string k = lowerString(s);
  if (parseOnOff(k, out))
    return true;
  if (k == "close" || k == "closed")
    out = 0x01;
  else if (k == "white")
    out = 0x02;
  else if (k == "red")
    out = 0x03;
  else if (k == "green")
    out = 0x04;
  else if (k == "pink")
    out = 0x05;
  else if (k == "purple")
    out = 0x06;
  else if (k == "blue")
    out = 0x07;
  else if (k == "yellow")
    out = 0x08;
  else if (k == "flicker-white" || k == "blink-white")
    out = 0x12;
  else if (k == "flicker-red" || k == "blink-red")
    out = 0x13;
  else if (k == "flicker-green" || k == "blink-green")
    out = 0x14;
  else if (k == "flicker-pink" || k == "blink-pink")
    out = 0x15;
  else if (k == "flicker-purple" || k == "blink-purple")
    out = 0x16;
  else if (k == "flicker-blue" || k == "blink-blue")
    out = 0x17;
  else if (k == "flicker-yellow" || k == "blink-yellow")
    out = 0x18;
  else if (k == "flicker-random" || k == "blink-random")
    out = 0x19;
  else if (k == "flicker-random-three-group" ||
           k == "blink-random-three-group")
    out = 0x20;
  else if (k == "head-breathing-end")
    out = 0x00;
  else if (k == "head-breathing-start")
    out = 0x01;
  else if (k == "head-wakeup")
    out = 0x04;
  else if (k == "head-mute-start")
    out = 0x07;
  else if (k == "head-video-start")
    out = 0x03;
  else if (k == "head-sleep")
    out = 0x0A;
  else if (k == "head-video-end")
    out = 0x14;
  else if (k == "head-human-control-end")
    out = 0x1E;
  else if (k == "head-human-control-start")
    out = 0x1F;
  else if (k == "head-animated-breathing")
    out = 0x18;
  else
    return parseByteValue(s, out);
  return true;
}

static void printUsage(const char *argv0) {
  fprintf(stderr,
          "Usage:\n"
          "  %s help\n"
          "  %s examples\n"
          "  %s [--debug] [--test] torch on|off|restore [brightness]\n"
          "  %s [--debug] [--test] led TARGET on|off|MODE [rate] [random]\n"
          "  %s [--debug] [--test] face 1..21\n"
          "  %s [--debug] [--test] projector on|off|query\n"
          "  %s [--debug] [--test] speaker on|off\n"
          "  %s [--db PATH] [--debug] [--test] commands\n"
          "  %s [--db PATH] list-all-commands [CATEGORY]\n"
          "  %s [--db PATH] describe-command NAME\n"
          "  %s [--db PATH] [--target head|bottom|both] [--debug] [--test] "
          "send-command NAME key=value...\n"
          "  %s [--test] take-control\n"
          "  %s [--test] listen [seconds]\n"
          "  %s [--debug] [--test] <legacy-command> ...\n",
          argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0,
          argv0, argv0, argv0, argv0, argv0);
}

static void printExamples(const char *argv0) {
  printf("Quick commands:\n");
  printf("  %s commands\n", argv0);
  printf("  %s list-all-commands\n", argv0);
  printf("  %s list-all-commands PeripheralControl\n", argv0);
  printf("  %s describe-command wheel\n", argv0);
  printf("  %s --test --debug send-command wheel mode=distance "
         "direction=forward speed=50 distance=1000\n",
         argv0);
  printf("  %s torch restore 5\n", argv0);
  printf("  %s led head on\n", argv0);
  printf("  %s face 1\n", argv0);
  printf("  %s projector on\n", argv0);
  printf("  %s speaker on\n", argv0);
  printf("  %s take-control\n", argv0);
  printf("  %s listen\n", argv0);
  printf("\n");

  printf("Where commands come from:\n");
  printf("  The command catalogue is loaded from "
         "mcu-command-database/sanbot_mcu_commands.sqlite.\n");
  printf("  Use commands to list names and describe-command NAME to see "
         "accepted fields.\n");
  printf("  Use list-all-commands to list categories, then pass a category "
         "to see descriptions.\n");
  printf("  Override the database with --db PATH or SANBOT_MCU_COMMAND_DB.\n");
  printf("\n");

  printf("Locomotion examples:\n");
  printf("  %s send-command wheel mode=distance direction=forward "
         "speed=50 distance=1000\n",
         argv0);
  printf("  %s send-command wheel mode=relative direction=left speed=40 "
         "angle=90\n",
         argv0);
  printf("  %s send-command wheel mode=timed direction=turn-left "
         "time=1000 degree=90\n",
         argv0);
  printf("  %s send-command wheel mode=no-angle "
         "direction=right-translation speed=40 time=1000 isCircle=0\n",
         argv0);
  printf("  %s send-command wheel mode=no-angle direction=stop speed=0 "
         "time=0 isCircle=0\n",
         argv0);
  printf("\n");

  printf("Light examples:\n");
  printf("  %s torch on\n", argv0);
  printf("  %s torch off\n", argv0);
  printf("  %s torch restore 5\n", argv0);
  printf("  %s led all on\n", argv0);
  printf("  %s led left-arm blue\n", argv0);
  printf("  %s led right-ear flicker-purple 2 0\n", argv0);
  printf("  %s led head 0x18 2 0\n", argv0);
  printf("  %s send-command LEDLightCommand whichLight=1 switchMode=on "
         "led_rate=5 led_random_number=0\n",
         argv0);
  printf("  %s send-command WhiteLightCommand switchMode=on\n", argv0);
  printf("  %s send-command SetWhiteBrightness setWhiteBrightness=restore "
         "brightness=5\n",
         argv0);
  printf("  %s send-command SetWhiteBrightness brightness=5\n", argv0);
  printf("  %s send-command QueryWhiteBrightness\n", argv0);
  printf("\n");

  printf("Face examples:\n");
  printf("  %s face 1\n", argv0);
  printf("  %s send-command LiliNormalExpression expression_type=face-1\n",
         argv0);
  printf("\n");

  printf("Projector and speaker examples:\n");
  printf("  %s projector on\n", argv0);
  printf("  %s projector off\n", argv0);
  printf("  %s projector query\n", argv0);
  printf("  %s speaker on\n", argv0);
  printf("  %s speaker off\n", argv0);
  printf("  %s send-command ProjectorCommand switchMode=on\n", argv0);
  printf("  %s send-command SpeakerCommand switchMode=on\n", argv0);
  printf("\n");

  printf("Battery examples:\n");
  printf("  %s send-command QueryBatteryCommand battery=0 "
         "currentBattery=0\n",
         argv0);
  printf("  %s send-command BatteryTemperatureCommand temperature=0\n",
         argv0);
  printf("  %s send-command AutoBatteryCommand switchMode=on threshold=20\n",
         argv0);
  printf("  %s send-command AutoBatteryCommand switchMode=off\n", argv0);
}

static string defaultDatabasePath(const char *argv0) {
  namespace fs = std::filesystem;
  fs::path exe = fs::absolute(argv0);
  return sanbot::CommandDatabase::findDefaultDatabasePath(
      exe.has_parent_path() ? exe.parent_path().string() : string{});
}

static void printCommandList(const sanbot::CommandDatabase &db) {
  for (const auto &command : db.commands()) {
    printf("%-30s %-22s", command.canonicalName.c_str(),
           command.commandGroup.c_str());
    if (!command.aliases.empty()) {
      printf(" aliases:");
      for (const auto &alias : command.aliases)
        printf(" %s", alias.c_str());
    }
    printf("\n");
  }
}

static void printCommandCategories(const sanbot::CommandDatabase &db) {
  map<string, size_t> counts;
  for (const auto &command : db.commands())
    counts[command.commandGroup]++;

  printf("Command categories:\n");
  for (const auto &[group, count] : counts)
    printf("  %-24s %zu commands\n", group.c_str(), count);
  printf("\nUse list-all-commands CATEGORY to see commands in a category.\n");
}

static void printCommandsInCategory(const sanbot::CommandDatabase &db,
                                    const string &category) {
  string wanted = lowerString(category);
  bool found = false;
  for (const auto &command : db.commands()) {
    if (lowerString(command.commandGroup) != wanted)
      continue;
    found = true;
    printf("%-30s %s\n", command.canonicalName.c_str(),
           command.description.c_str());
    if (!command.aliases.empty()) {
      printf("  aliases:");
      for (const auto &alias : command.aliases)
        printf(" %s", alias.c_str());
      printf("\n");
    }
  }

  if (!found) {
    fprintf(stderr, "smb: unknown command category: %s\n", category.c_str());
    printCommandCategories(db);
  }
}

static void printCommandDescription(const sanbot::CommandInfo &command) {
  printf("%s\n", command.canonicalName.c_str());
  printf("  group: %s\n", command.commandGroup.c_str());
  printf("  target: %s\n", command.targetName.c_str());
  printf("  ack: %s\n", command.ackDefaultHex.c_str());
  if (!command.routeTagHex.empty())
    printf("  route tag: %s\n", command.routeTagHex.c_str());
  printf("  aliases:");
  for (const auto &alias : command.aliases)
    printf(" %s", alias.c_str());
  printf("\n");
  printf("  template: %s\n", command.payloadTemplate.c_str());
  printf("  parameters:\n");
  for (const auto &parameter : command.parameters) {
    if (parameter.fieldName == "commandMode" ||
        parameter.fieldRole == "command_mode")
      continue;
    printf("    %-28s role=%s", parameter.fieldName.c_str(),
           parameter.fieldRole.c_str());
    if (!parameter.valueHex.empty())
      printf(" const=%s", parameter.valueHex.c_str());
    else if (!parameter.valueExpr.empty())
      printf(" value=%s", parameter.valueExpr.c_str());
    if (!parameter.conditionExpr.empty())
      printf(" when %s", parameter.conditionExpr.c_str());
    printf("\n");

    auto keys = parameter.valueHex.empty() ? sanbot::commandArgumentKeys(parameter)
                                           : vector<string>{};
    if (!keys.empty()) {
      printf("      args:");
      for (const auto &key : keys)
        printf(" %s", key.c_str());
      printf("\n");
    }

    auto values = sanbot::commandValueAliases(command, parameter);
    if (!values.empty()) {
      printf("      values:");
      for (const auto &[name, byte] : values)
        printf(" %s=0x%02X", name.c_str(), byte);
      printf("\n");
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  bool debug = false;
  bool test = false;
  string dbPath;
  string directTarget;
  int argi = 1;
  while (argi < argc) {
    string flag = argv[argi];
    if (flag == "--debug" || flag == "--verbose") {
      debug = true;
      argi++;
      continue;
    }
    if (flag == "--test" || flag == "--dry-run") {
      test = true;
      argi++;
      continue;
    }
    if (flag == "--db") {
      if (argi + 1 >= argc) {
        printUsage(argv[0]);
        return 1;
      }
      dbPath = argv[argi + 1];
      argi += 2;
      continue;
    }
    if (flag == "--target") {
      if (argi + 1 >= argc) {
        printUsage(argv[0]);
        return 1;
      }
      directTarget = lowerString(argv[argi + 1]);
      argi += 2;
      continue;
    }
    if (flag == "--help" || flag == "-h") {
      printUsage(argv[0]);
      return 0;
    }
    break;
  }

  if (argi >= argc) {
    printUsage(argv[0]);
    return 1;
  }

  string cmd = lowerString(argv[argi]);
  unique_ptr<SanbotUsbManager> manager;

  if (cmd == "help") {
    printUsage(argv[0]);
    printf("\n");
    printExamples(argv[0]);
    return 0;
  }

  if (cmd == "examples" || cmd == "quickstart") {
    printExamples(argv[0]);
    return 0;
  }

  auto open_database = [&]() {
    return sanbot::CommandDatabase(
        dbPath.empty() ? defaultDatabasePath(argv[0]) : dbPath);
  };

  auto ensure_manager = [&]() -> SanbotUsbManager * {
    if (!test && !manager)
      manager = make_unique<SanbotUsbManager>();
    return manager.get();
  };

  auto send_packet = [&](const vector<uint8_t> &packet) {
    vector<unsigned char> buf(packet.begin(), packet.end());
    if (!test) {
      SanbotUsbManager *usb = ensure_manager();
      usb->sendToPoint(buf);
      usb->waitForPendingSends();
    }
    if (debug) log_packet(buf);
    if (test) {
      printf("[TEST] Skipped USB send\n");
      fflush(stdout);
    }
  };

  auto send_built_command = [&](const sanbot::BuiltCommand &built) -> bool {
    vector<unsigned char> buf(built.bytes.begin(), built.bytes.end());
    if (!test) {
      SanbotUsbManager *usb = ensure_manager();
      if (built.hasRouteTag()) {
        usb->sendToPoint(buf);
      } else if (directTarget == "head") {
        usb->sendToHead(buf);
      } else if (directTarget == "bottom") {
        usb->sendToBottom(buf);
      } else if (directTarget == "both") {
        usb->sendToHead(buf);
        usb->sendToBottom(buf);
      } else {
        fprintf(stderr,
                "%s has no database route tag. Pass --target head, bottom, "
                "or both.\n",
                built.canonicalName.c_str());
        return false;
      }
      usb->waitForPendingSends();
    }
    if (debug) log_packet(buf);
    if (test) {
      printf("[TEST] Skipped USB send\n");
      fflush(stdout);
    }
    return true;
  };

  if (cmd == "torch" || cmd == "white-light") {
    if (argc - argi < 2 || argc - argi > 3) {
      printUsage(argv[0]);
      return 1;
    }

    string action = lowerString(argv[argi + 1]);
    uint8_t brightness = 0x05;
    if (argc - argi == 3 && !parseByteValue(argv[argi + 2], brightness))
      return 1;

    auto db = open_database();
    try {
      if (action == "off") {
        return send_built_command(db.buildCommand(
                   "WhiteLightCommand", {{"switch", "off"}}))
                   ? 0
                   : 1;
      }
      if (action == "on") {
        if (!send_built_command(
                db.buildCommand("WhiteLightCommand", {{"switch", "on"}})))
          return 1;
        return send_built_command(db.buildCommand(
                   "SetWhiteBrightness",
                   {{"setWhiteBrightness", "restore"},
                    {"brightness", to_string(static_cast<int>(brightness))}}))
                   ? 0
                   : 1;
      }
      if (action == "restore" || action == "brightness") {
        return send_built_command(db.buildCommand(
                   "SetWhiteBrightness",
                   {{"setWhiteBrightness", "restore"},
                    {"brightness", to_string(static_cast<int>(brightness))}}))
                   ? 0
                   : 1;
      }
    } catch (const exception &ex) {
      fprintf(stderr, "smb: %s\n", ex.what());
      return 1;
    }

    printUsage(argv[0]);
    return 1;
  }

  if (cmd == "led") {
    if (argc - argi < 3 || argc - argi > 5) {
      printUsage(argv[0]);
      return 1;
    }

    uint8_t target;
    if (!parseLedTarget(argv[argi + 1], target))
      return 1;

    uint8_t mode;
    if (!parseLedMode(argv[argi + 2], mode))
      return 1;

    uint8_t rate = 0x00;
    uint8_t random = 0x00;
    if (argc - argi >= 4 && !parseByteValue(argv[argi + 3], rate))
      return 1;
    if (argc - argi == 5 && !parseByteValue(argv[argi + 4], random))
      return 1;

    try {
      auto db = open_database();
      return send_built_command(db.buildCommand(
                 "LEDLightCommand",
                 {{"whichLight", to_string(static_cast<int>(target))},
                  {"switchMode", to_string(static_cast<int>(mode))},
                  {"led_rate", to_string(static_cast<int>(rate))},
                  {"led_random_number", to_string(static_cast<int>(random))}}))
                 ? 0
                 : 1;
    } catch (const exception &ex) {
      fprintf(stderr, "smb: %s\n", ex.what());
      return 1;
    }
  }

  if (cmd == "face" || cmd == "expression") {
    if (argc - argi != 2) {
      printUsage(argv[0]);
      return 1;
    }

    uint8_t expression;
    if (!parseFaceMode(argv[argi + 1], expression) || expression < 1 ||
        expression > 21)
      return 1;

    try {
      auto db = open_database();
      return send_built_command(db.buildCommand(
                 "LiliNormalExpression",
                 {{"expression_type", to_string(static_cast<int>(expression))}}))
                 ? 0
                 : 1;
    } catch (const exception &ex) {
      fprintf(stderr, "smb: %s\n", ex.what());
      return 1;
    }
  }

  if (cmd == "projector") {
    if (argc - argi != 2) {
      printUsage(argv[0]);
      return 1;
    }

    string action = lowerString(argv[argi + 1]);
    try {
      auto db = open_database();
      if (action == "query" || action == "status") {
        return send_built_command(db.buildCommand("QueryProjectorSwitch", {}))
                   ? 0
                   : 1;
      }
      uint8_t switchMode;
      if (!parseOnOff(action, switchMode))
        return 1;
      return send_built_command(db.buildCommand(
                 "ProjectorCommand",
                 {{"switchMode", to_string(static_cast<int>(switchMode))}}))
                 ? 0
                 : 1;
    } catch (const exception &ex) {
      fprintf(stderr, "smb: %s\n", ex.what());
      return 1;
    }
  }

  if (cmd == "speaker") {
    if (argc - argi != 2) {
      printUsage(argv[0]);
      return 1;
    }

    uint8_t switchMode;
    if (!parseOnOff(argv[argi + 1], switchMode))
      return 1;

    try {
      auto db = open_database();
      return send_built_command(db.buildCommand(
                 "SpeakerCommand",
                 {{"switchMode", to_string(static_cast<int>(switchMode))}}))
                 ? 0
                 : 1;
    } catch (const exception &ex) {
      fprintf(stderr, "smb: %s\n", ex.what());
      return 1;
    }
  }

  if (cmd == "take-control" || cmd == "claim-usb") {
    if (argc - argi != 1) {
      printUsage(argv[0]);
      return 1;
    }
    if (test) {
      printf("[TEST] Skipped USB control claim\n");
      return 0;
    }
    SanbotUsbManager *usb = ensure_manager();
    if (!usb->takeControl()) {
      fprintf(stderr, "smb: no Sanbot USB endpoints claimed\n");
      return 1;
    }
    printf("Claimed Sanbot USB endpoints\n");
    return 0;
  }

  if (cmd == "listen") {
    if (argc - argi > 2) {
      printUsage(argv[0]);
      return 1;
    }
    int seconds = 0;
    if (argc - argi == 2) {
      try {
        seconds = stoi(argv[argi + 1], nullptr, 0);
      } catch (...) {
        return 1;
      }
      if (seconds < 0)
        return 1;
    }
    if (test) {
      printf("[TEST] Skipped USB listener\n");
      return 0;
    }

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    SanbotUsbManager *usb = ensure_manager();
    usb->setListener(log_received);
    if (!usb->takeControl()) {
      fprintf(stderr, "smb: no Sanbot USB endpoints claimed\n");
      return 1;
    }
    usb->startListener();
    printf("Listening for Sanbot USB packets");
    if (seconds > 0)
      printf(" for %d seconds", seconds);
    printf(". Press Ctrl-C to stop.\n");
    fflush(stdout);

    auto start = chrono::steady_clock::now();
    while (!stopRequested) {
      if (seconds > 0) {
        auto elapsed = chrono::steady_clock::now() - start;
        if (chrono::duration_cast<chrono::seconds>(elapsed).count() >= seconds)
          break;
      }
      this_thread::sleep_for(chrono::milliseconds(100));
    }
    usb->stopListener();
    return 0;
  }

  try {
    if (cmd == "commands" || cmd == "list-commands" || cmd == "db-list") {
      auto db = open_database();
      printCommandList(db);
      return 0;
    }

    if (cmd == "list-all-commands" || cmd == "categories" ||
        cmd == "command-categories") {
      if (argc - argi > 2) {
        printUsage(argv[0]);
        return 1;
      }
      auto db = open_database();
      if (argc - argi == 1)
        printCommandCategories(db);
      else
        printCommandsInCategory(db, argv[argi + 1]);
      return 0;
    }

    if (cmd == "describe-command" || cmd == "describe" ||
        cmd == "db-describe") {
      if (argc - argi != 2) {
        printUsage(argv[0]);
        return 1;
      }
      auto db = open_database();
      printCommandDescription(db.resolveCommand(argv[argi + 1]));
      return 0;
    }

    if (cmd == "send-command" || cmd == "db-send" || cmd == "command") {
      if (argc - argi < 2) {
        printUsage(argv[0]);
        return 1;
      }
      vector<string> tokens;
      for (int i = argi + 2; i < argc; ++i)
        tokens.push_back(argv[i]);
      auto db = open_database();
      auto built =
          db.buildCommand(argv[argi + 1], sanbot::parseCommandArgs(tokens));
      return send_built_command(built) ? 0 : 1;
    }
  } catch (const exception &ex) {
    fprintf(stderr, "smb: %s\n", ex.what());
    return 1;
  }

  if (cmd == "hex-send") {
    if (argc - argi < 2)
      return 1;
    vector<uint8_t> bytes;
    for (int i = argi + 1; i < argc; ++i) {
      uint8_t byte;
      try {
        int val = stoi(argv[i], nullptr, 16);
        if (val < 0 || val > 255)
          return 1;
        byte = static_cast<uint8_t>(val);
      } catch (...) {
        return 1;
      }
      bytes.push_back(byte);
    }
    send_packet(bytes);
    return 0;
  }

  if (cmd == "wheel-distance") {
    if (argc - argi != 4)
      return 1;
    uint8_t action, speed;
    uint16_t distance;
    if (!parseWheelAction(argv[argi + 1], action))
      return 1;
    if (!parseByteValue(argv[argi + 2], speed))
      return 1;
    if (!parseU16Value(argv[argi + 3], distance))
      return 1;
    send_packet(buildWheelDistance(action, speed, distance));
    return 0;
  }

  if (cmd == "wheel-relative") {
    if (argc - argi != 4)
      return 1;
    uint8_t action, speed;
    uint16_t angle;
    if (!parseWheelAction(argv[argi + 1], action))
      return 1;
    if (!parseByteValue(argv[argi + 2], speed))
      return 1;
    if (!parseU16Value(argv[argi + 3], angle))
      return 1;
    send_packet(buildWheelRelativeAngle(action, speed, angle));
    return 0;
  }

  if (cmd == "wheel-no-angle") {
    if (argc - argi != 5)
      return 1;
    uint8_t action, speed, durationMode;
    uint16_t duration;
    if (!parseWheelAction(argv[argi + 1], action))
      return 1;
    if (!parseByteValue(argv[argi + 2], speed))
      return 1;
    if (!parseU16Value(argv[argi + 3], duration))
      return 1;
    if (!parseByteValue(argv[argi + 4], durationMode))
      return 1;
    send_packet(buildWheelNoAngle(action, speed, duration, durationMode));
    return 0;
  }

  if (cmd == "wheel-timed") {
    if (argc - argi != 4)
      return 1;
    uint8_t action, degree;
    uint16_t time;
    if (!parseWheelAction(argv[argi + 1], action))
      return 1;
    if (!parseU16Value(argv[argi + 2], time))
      return 1;
    if (!parseByteValue(argv[argi + 3], degree))
      return 1;
    send_packet(buildWheelTimed(action, time, degree));
    return 0;
  }

  if (cmd == "arm-no-angle") {
    if (argc - argi != 4)
      return 1;
    uint8_t part, speed, action;
    if (!parseArmPart(argv[argi + 1], part))
      return 1;
    if (!parseByteValue(argv[argi + 2], speed))
      return 1;
    if (!parseArmAction(argv[argi + 3], action))
      return 1;
    send_packet(buildArmNoAngle(part, speed, action));
    return 0;
  }

  if (cmd == "arm-relative") {
    if (argc - argi != 5)
      return 1;
    uint8_t part, speed, action;
    uint16_t angle;
    if (!parseArmPart(argv[argi + 1], part))
      return 1;
    if (!parseByteValue(argv[argi + 2], speed))
      return 1;
    if (!parseArmAction(argv[argi + 3], action))
      return 1;
    if (!parseU16Value(argv[argi + 4], angle))
      return 1;
    send_packet(buildArmRelativeAngle(part, speed, action, angle));
    return 0;
  }

  if (cmd == "arm-absolute") {
    if (argc - argi != 4)
      return 1;
    uint8_t part, speed;
    uint16_t angle;
    if (!parseArmPart(argv[argi + 1], part))
      return 1;
    if (!parseByteValue(argv[argi + 2], speed))
      return 1;
    if (!parseU16Value(argv[argi + 3], angle))
      return 1;
    send_packet(buildArmAbsoluteAngle(part, speed, angle));
    return 0;
  }

  if (cmd == "head-no-angle") {
    if (argc - argi != 3)
      return 1;
    uint8_t action, speed;
    if (!parseHeadAction(argv[argi + 1], action))
      return 1;
    if (!parseByteValue(argv[argi + 2], speed))
      return 1;
    send_packet(buildHeadNoAngle(action, speed));
    return 0;
  }

  if (cmd == "head-relative") {
    if (argc - argi != 3 && argc - argi != 2)
      return 1;
    uint8_t action;
    if (!parseHeadAction(argv[argi + 1], action))
      return 1;
    if (argc - argi == 2) {
      if (action != 0x09 && action != 0x0A && action != 0x0B)
        return 1;
      send_packet(buildHeadNoAngle(action, 0x00));
      return 0;
    }
    uint16_t angle;
    if (!parseU16Value(argv[argi + 2], angle))
      return 1;
    send_packet(buildHeadRelativeAngle(action, angle));
    return 0;
  }

  if (cmd == "head-absolute") {
    if (argc - argi != 3)
      return 1;
    uint8_t action;
    uint16_t angle;
    if (!parseHeadAbsoluteAction(argv[argi + 1], action))
      return 1;
    if (!parseU16Value(argv[argi + 2], angle))
      return 1;
    send_packet(buildHeadAbsoluteAngle(action, angle));
    return 0;
  }

  if (cmd == "head-locate-absolute") {
    if (argc - argi != 4)
      return 1;
    uint8_t action;
    uint16_t hAngle, vAngle;
    if (!parseHeadLockAction(argv[argi + 1], action))
      return 1;
    if (!parseU16Value(argv[argi + 2], hAngle))
      return 1;
    if (!parseU16Value(argv[argi + 3], vAngle))
      return 1;
    send_packet(buildHeadLocateAbsolute(action, hAngle, vAngle));
    return 0;
  }

  if (cmd == "head-locate-relative") {
    if (argc - argi != 6)
      return 1;
    uint8_t action, hAngle, vAngle, hDirection, vDirection;
    if (!parseHeadLockAction(argv[argi + 1], action))
      return 1;
    if (!parseByteValue(argv[argi + 2], hAngle))
      return 1;
    if (!parseByteValue(argv[argi + 3], vAngle))
      return 1;
    if (!parseHeadDirection(argv[argi + 4], hDirection))
      return 1;
    if (!parseHeadDirection(argv[argi + 5], vDirection))
      return 1;
    send_packet(buildHeadLocateRelative(action, hAngle, vAngle,
                                        hDirection, vDirection));
    return 0;
  }

  if (cmd == "head-centre") {
    send_packet(buildHeadCentreLock());
    return 0;
  }

  try {
    vector<string> tokens;
    for (int i = argi + 1; i < argc; ++i)
      tokens.push_back(argv[i]);
    auto db = open_database();
    auto built = db.buildCommand(argv[argi], sanbot::parseCommandArgs(tokens));
    return send_built_command(built) ? 0 : 1;
  } catch (const exception &ex) {
    fprintf(stderr, "smb: %s\n", ex.what());
  }

  return 1;
}
