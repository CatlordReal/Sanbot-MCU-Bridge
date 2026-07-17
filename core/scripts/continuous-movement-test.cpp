#include <chrono>
#include <cstdlib>
#include <iostream>;
#include <thread>

using namespace std;

int main() {
  int result = 0;
  string command;
  while (result = 0) {
    result = system("./smb take-control");
    string delay, angle, speed, direction;
    int repeats;

    cout << "Repeats: ";
    cin >> repeats;
    cout << "\nDelay: ";
    cin >> delay;
    cout << "\nAngle: ";
    cin >> angle;
    cout << "\nSpeed:";
    cin >> speed;
    cout << "Direction (up/down)";
    cin >> direction;

    for (int i = 0; i < repeats; i++) {
      result = system(
          ("./smb arm-relative left " + speed + " " + direction + " " + angle)
              .c_str());
    }
    result = system("./smb arm-no-angle left 1 stop");
  }
}
