#include "application.hpp"

int main() {
  auto app = betty::make_application();
  if (!app) return 1;
  return app->run();
}
