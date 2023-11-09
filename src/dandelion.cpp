#include <atomic>
#include <cmath>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <raylib.h>
#include <raymath.h>

constexpr int win_width = 800;
constexpr int win_height = 800;
constexpr int segments = 100;

Vector2 camera = {0.0f, 0.0f};
float zoom = 1.0f;

// std::mutex random_mutex;
std::mt19937 mtrandom;
std::normal_distribution germination_dist(17.0f, 1.0f);
std::normal_distribution mature_dist(30.0f, 2.7f);
std::normal_distribution flower_dist(50.0f, 2.7f);
std::normal_distribution wither_dist(10.0f, 1.4f);
std::normal_distribution puffball_dist(15.0f, 1.4f);
std::uniform_int_distribution<int> sub_mature_dist(300, 400);
std::uniform_int_distribution<int> wind_dist_dist(1, 56);
std::normal_distribution wind_angle_dist(0.0f, 20.0f);
std::uniform_int_distribution<int> seeds_dist(1500, 2000);

struct Dandelion {
  enum class Stage {
    Germinating,
    Maturing,
    Flowering,
    Withering,
    Puffball,
    SubsequentMaturing
  };

  int age;
  int days_since_last_stage;
  Stage stage;
  
  int germination_time;
  int mature_time;
  int flower_time;
  int wither_time;
  int puffball_time;
  int sub_mature_time;

  Dandelion() : age(0), days_since_last_stage(0), stage(Stage::Germinating) {
    // random_mutex.lock();
    germination_time = germination_dist(mtrandom);
    mature_time = mature_dist(mtrandom);
    flower_time = flower_dist(mtrandom);
    wither_time = wither_dist(mtrandom);
    puffball_time = puffball_dist(mtrandom);
    // random_mutex.unlock();
  }
};

std::mutex grid_mutex;
std::vector<Dandelion> grid[segments][segments];

std::atomic<uint64_t> day = 0;
std::atomic<float> physics_delta_time = 0.0f;
std::atomic<bool> should_close = false;

void simulate() {
  float delta_time = 0.0f;
  auto last_frame = std::chrono::steady_clock::now();

  while (!should_close) {
    grid_mutex.lock();
    for (int y = 0; y < segments; ++y) {
      for (int x = 0; x < segments; ++x) {
        for (auto &dandelion : grid[y][x]) {
          switch(dandelion.stage) {
            case Dandelion::Stage::Germinating:
              if (dandelion.days_since_last_stage >= dandelion.germination_time) {
                dandelion.stage = Dandelion::Stage::Maturing;
                dandelion.days_since_last_stage = 0;
              }
              break;
            case Dandelion::Stage::Maturing:
              if (dandelion.days_since_last_stage >= dandelion.mature_time) {
                dandelion.stage = Dandelion::Stage::Flowering;
                dandelion.days_since_last_stage = 0;
              }
              break;
            case Dandelion::Stage::Flowering:
              if (dandelion.days_since_last_stage >= dandelion.flower_time) {
                dandelion.stage = Dandelion::Stage::Withering;
                dandelion.days_since_last_stage = 0;
              }
              break;
            case Dandelion::Stage::Withering:
              if (dandelion.days_since_last_stage >= dandelion.wither_time) {
                dandelion.stage = Dandelion::Stage::Puffball;
                dandelion.days_since_last_stage = 0;
              }
              break;
            case Dandelion::Stage::Puffball:
              if (dandelion.days_since_last_stage >= dandelion.puffball_time) {
                int seeds = seeds_dist(mtrandom);
                for (int i = 0; i < seeds; ++i) {
                  int dist = wind_dist_dist(mtrandom);
                  int angle = std::round(wind_angle_dist(mtrandom));
                  int movex = std::round(dist * std::sin(angle * DEG2RAD));
                  int movey = std::round(dist * std::cos(angle * DEG2RAD));
                  int newx = x+movex;
                  int newy = y-movey;
                  if (newx >= 0 && newx < segments && newy >= 0 && newy < segments) {
                    grid[newy][newx].push_back(Dandelion());
                  }
                }
                dandelion.stage = Dandelion::Stage::SubsequentMaturing;
                dandelion.sub_mature_time = sub_mature_dist(mtrandom);
                dandelion.days_since_last_stage = 0;
              }
              break;
            case Dandelion::Stage::SubsequentMaturing:
              if (dandelion.days_since_last_stage >= dandelion.sub_mature_time) {
                dandelion.stage = Dandelion::Stage::Flowering;
                dandelion.days_since_last_stage = 0;
              }
              break;
            default:
              break;
          }
          dandelion.age++;
          dandelion.days_since_last_stage++;
        }
      }
    }
    grid_mutex.unlock();
    day++;

    auto dur = std::chrono::steady_clock::now() - last_frame;
    std::this_thread::sleep_for(
        std::chrono::duration<float, std::milli>(500.0f) - dur);

    auto current_frame = std::chrono::steady_clock::now();
    delta_time =
        std::chrono::duration<float, std::milli>(current_frame - last_frame)
            .count();
    last_frame = current_frame;
    physics_delta_time = delta_time;
  }
}

Vector2 transform_point(Vector2 in) {
  return {(zoom*(in.x - camera.x)) + win_width/2.0f, -(zoom*(in.y - camera.y)) + win_height/2.0f};
}

Vector2 transform_size(Vector2 in) {
  return {zoom*in.x, zoom*in.y};
}

int main() {
  std::random_device real_random;

  mtrandom = std::mt19937(real_random());
  // std::uniform_int_distribution<int> uniform_dist(0, 10);
  // for (int y = 0; y < segments; ++y) {
  //   for (int x = 0; x < segments; ++x) {
  //     for (int i = 0; i < uniform_dist(mtrandom); ++i) {
  //       grid[y][x].push_back({0, 0});
  //     }
  //   }
  // }

  // FIRST DANDELION
  grid[48][48].push_back(Dandelion());

  std::cout << "hello world" << std::endl;

  SetConfigFlags(FLAG_MSAA_4X_HINT);
  InitWindow(win_width, win_height, "dandelion");
  SetTargetFPS(60);

  std::thread simulate_thread(simulate);

  while (!WindowShouldClose()) {
    if (IsKeyDown(KEY_RIGHT)) camera.x += 2.0f;
    if (IsKeyDown(KEY_LEFT)) camera.x -= 2.0f;
    if (IsKeyDown(KEY_UP)) camera.y += 2.0f;
    if (IsKeyDown(KEY_DOWN)) camera.y -= 2.0f;

    if (IsKeyDown(KEY_EQUAL)) zoom *= 1.02f;
    if (IsKeyDown(KEY_MINUS)) zoom /= 1.02f;
    
    BeginDrawing();
    ClearBackground(RAYWHITE);

    Vector2 top_left = {-(win_width/2.0f), (win_height/2.0f)};
    Vector2 size = {win_width, win_height};
    DrawRectangleV(transform_point(top_left), transform_size(size), WHITE);

    for (int y = 0; y < segments; ++y) {
      for (int x = 0; x < segments; ++x) {
        int diff = grid[y][x].size()*20;
        if (diff == 0) {
          continue;
        }
        Color c = {(unsigned char)(255-diff), (unsigned char)(255-diff), (unsigned char)(255-diff), 255};
        Vector2 top_left = {(float)(-400 + x*8), (float)(400 - y*8)};
        DrawRectangleV(transform_point(top_left), transform_size({8.0f, 8.0f}), c);
      }
    }

    Vector2 c = {100.0f, 0.0f};
    DrawCircleV(transform_point(c), 20.0f, BLACK);

    std::cout << "day: " << day << "         \r";

    EndDrawing();
  }
  should_close = true;
  simulate_thread.join();
  CloseWindow();
  return 0;
}