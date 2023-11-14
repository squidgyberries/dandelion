#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <forward_list>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <vector>

#include <raylib.h>
#include <raymath.h>

// #include "iconset.rgi.h"
#define RAYGUI_CUSTOM_ICONS
#include "raygui.h"

#include <fmt/core.h>

#include "csv.h"

#include "stb_image_write.h"

constexpr int clamp(int value, int min, int max) {
  int t = value > min ? value : min;
  return t < max ? t : max;
}

constexpr int view_width = 800;
constexpr int view_height = 800;

constexpr int top_bar_height = 40;
constexpr int bottom_bar_height = 40;

constexpr int win_width = view_width;
constexpr int win_height = top_bar_height + view_height + bottom_bar_height;

constexpr int segments = 100;
constexpr int day_length = 1000.0f;

Vector2 camera = {0.0f, 0.0f};
double zoom = 1.0;
constexpr double zoom_mult = 1.02;

io::CSVReader<5> *reader;

std::mt19937 mtrandom;
std::mt19937 mtrandom1;
std::mt19937 mtrandom2;
std::mt19937 mtrandom3;
std::mt19937 mtrandom4;
std::normal_distribution germination_dist(17.0f, 1.0f);
std::normal_distribution mature_dist(30.0f, 2.7f);
std::normal_distribution flower_dist(50.0f, 2.7f);
std::normal_distribution wither_dist(10.0f, 1.4f);
std::normal_distribution puffball_dist(15.0f, 1.4f);
std::uniform_int_distribution<int> sub_mature_dist(300, 400);
std::normal_distribution wind_dist_dist(0.0f, 6.0f);
std::normal_distribution wind_angle_dist_normal(0.0f, 20.0f);
std::uniform_int_distribution<int> wind_angle_dist_uniform(0, 359);
std::uniform_int_distribution<int> seeds_dist(1500, 2000);
constexpr int seedling_eaten_chance = 55;
std::uniform_int_distribution<int> hundred_dist(1, 100);
std::atomic<int> ratio = 1;

struct Dandelion {
  enum class Stage : uint8_t {
    Germinating,
    Maturing,
    Flowering,
    Withering,
    Puffball,
    SubsequentMaturing
  };

  uint16_t age = 0;
  uint16_t days_since_last_stage = 0;
  Stage stage = Stage::Germinating;
  uint8_t health = 50;

  uint8_t germination_time = 17;
  uint8_t mature_time = 30;
  uint8_t flower_time = 50;
  uint8_t wither_time = 10;
  uint8_t puffball_time = 15;
  uint16_t sub_mature_time = 350;
  bool is_first = false;

  Dandelion() = delete;

  Dandelion(std::mt19937 &mt)
      : age(0), days_since_last_stage(0), stage(Stage::Germinating), health(50),
        germination_time(germination_dist(mt)), mature_time(mature_dist(mt)),
        flower_time(flower_dist(mt)), wither_time(wither_dist(mt)),
        puffball_time(puffball_dist(mt)), sub_mature_time(sub_mature_dist(mt)),
        is_first(false) {}

  inline uint8_t egermination_time() {
    if (health >= 50) {
      return germination_time;
    }
    return (1.0f - (float)(50 - health) / 100.0f) * germination_time;
  }
  inline uint8_t emature_time() {
    if (health >= 50) {
      return mature_time;
    }
    return (1.0f - (float)(50 - health) / 100.0f) * mature_time;
  }
  inline uint8_t eflower_time() {
    if (health >= 50) {
      return flower_time;
    }
    return (1.0f - (float)(50 - health) / 100.0f) * flower_time;
  }
  inline uint8_t ewither_time() {
    if (health >= 50) {
      return wither_time;
    }
    return (1.0f - (float)(50 - health) / 100.0f) * wither_time;
  }
  inline uint8_t epuffball_time() {
    if (health >= 50) {
      return puffball_time;
    }
    return (1.0f - (float)(50 - health) / 100.0f) * puffball_time;
  }
  inline uint8_t esub_mature_time() {
    if (health >= 50) {
      return sub_mature_time;
    }
    return (1.0f - (float)(50 - health) / 100.0f) * sub_mature_time;
  }
};

struct GridCoords {
  int x, y;
};
struct NewSeed {
  GridCoords coords;
  Dandelion dandelion;
};

std::atomic<int> full_grid[segments][segments];
std::atomic<uint64_t> total_dandelion_number = 0;

// top left
std::mutex grid1_mutex;
std::condition_variable cv1;
std::atomic<int> cv1_state = 0;
std::vector<Dandelion> grid1[segments / 2][segments / 2];
std::queue<NewSeed> grid1_seed_queue;

// top right
std::mutex grid2_mutex;
std::condition_variable cv2;
std::atomic<int> cv2_state = 0;
std::vector<Dandelion> grid2[segments / 2][segments / 2];
std::queue<NewSeed> grid2_seed_queue;

// bottom left
std::mutex grid3_mutex;
std::condition_variable cv3;
std::atomic<int> cv3_state = 0;
std::vector<Dandelion> grid3[segments / 2][segments / 2];
std::queue<NewSeed> grid3_seed_queue;

// bottom right
std::mutex grid4_mutex;
std::condition_variable cv4;
std::atomic<int> cv4_state = 0;
std::vector<Dandelion> grid4[segments / 2][segments / 2];
std::queue<NewSeed> grid4_seed_queue;

std::atomic<uint64_t> day = 1;
std::atomic<bool> should_close = false;
std::atomic<bool> paused = false;
std::vector<std::string> snap_dates;

constexpr int humidities[4][5] = {{73, 70, 87, 60, 49},
                                  {79, 50, 88, 58, 53},
                                  {90, 48, 89, 50, 58},
                                  {86, 57, 89, 62, 49}};
constexpr float lights[4][5] = {
    {2.0f + (1.0f / 6.0f), 8.5f, 10.0f + (5.0f / 6.0f), 9.5f, 10.5f},
    {17.5f, 13.0f + (1.0f / 6.0f), 11.5f, 12.25f, 11.0f + (5.0f / 6.0f)},
    {22.25f, 15.75f, 10.0f + (52.0f / 60.0f), 13.25f, 13.0f + (5.0f / 6.0f)},
    {13.0f, 10.0f + (2.0f / 3.0f), 10.75f, 11.75f, 11.25f}};

enum class Climate { Polar = 0, Continental, Tropical, Desert, Temperate };
std::atomic<Climate> climate = Climate::Temperate;
std::string date;
enum class Season { Winter = 0, Spring, Summer, Autumn };
const std::string season_strings[] = {"Winter", "Spring", "Summer", "Autumn"};
std::atomic<Season> season = Season::Winter;
std::atomic<float> temperature = 0.0f;
std::atomic<float> precipitation = 0.0f;
std::atomic<int> wind_dir = 0;
std::atomic<float> wind_speed = 0.0f;
std::atomic<int> humidity = 0;
std::atomic<float> light = 0.0f;
float ntemperature;
float nprecipitation;
int nwind_dir;
float nwind_speed;

// 0: nothing, 1: seeds, 2: die
int handle_dandelion(Dandelion &dand, std::mt19937 &mt) {
  int rc = 0;
  if (dand.egermination_time() == 0 || dand.emature_time() == 0 ||
      dand.ewither_time() == 0 || dand.epuffball_time() == 0 ||
      dand.esub_mature_time() == 0) {
    return 2;
  }
  if (dand.stage == Dandelion::Stage::Germinating) {
    if (dand.days_since_last_stage >= dand.egermination_time()) {
      dand.stage = Dandelion::Stage::Maturing;
      dand.days_since_last_stage = 0;
      dand.health += 50;
    }
    int eaten_roll = hundred_dist(mt);
    if (eaten_roll <= seedling_eaten_chance / dand.egermination_time()) {
      return 2;
    }
  } else if (dand.stage == Dandelion::Stage::Maturing) {
    if (dand.days_since_last_stage >= dand.emature_time()) {
      dand.stage = Dandelion::Stage::Flowering;
      dand.days_since_last_stage = 0;
    }
  } else if (dand.stage == Dandelion::Stage::Flowering) {
    if (dand.days_since_last_stage >= dand.eflower_time()) {
      dand.stage = Dandelion::Stage::Withering;
      dand.days_since_last_stage = 0;
    }
  } else if (dand.stage == Dandelion::Stage::Withering) {
    if (dand.days_since_last_stage >= dand.ewither_time()) {
      dand.stage = Dandelion::Stage::Puffball;
      dand.days_since_last_stage = 0;
    }
  } else if (dand.stage == Dandelion::Stage::Puffball) {
    if (dand.days_since_last_stage >= dand.epuffball_time()) {
      dand.stage = Dandelion::Stage::SubsequentMaturing;
      dand.days_since_last_stage = 0;
      rc = 1;
    }
  } else {
    if (dand.days_since_last_stage >= dand.esub_mature_time()) {
      dand.stage = Dandelion::Stage::Flowering;
      dand.days_since_last_stage = 0;
    }
  }

  if (precipitation < 0.7f) {
    dand.health -= 5;
  } else if (precipitation >= 0.7f && precipitation <= 1.4f) {
    dand.health -= 2;
  } else {
    dand.health = clamp(dand.health + 1, 0, 100);
  }

  if (temperature >= 5.0f) {
    dand.age++;
    dand.days_since_last_stage++;
  }
  if (temperature > 40.0f) {
    dand.health -= 7;
  } else if (temperature > 30.0f) {
    dand.health -= 1;
  } else if (temperature < 10.0f) {
    dand.health -= 1;
  } else {
    dand.health = clamp(dand.health + 2, 0, 100);
  }

  if (temperature > 30.0f && humidity < 60.0f) {
    dand.health -= 2;
  }
  if (humidity < 40.0f) {
    dand.health -= 2;
  }
  if (humidity >= 40.0f && humidity <= 80.0f) {
    dand.health = clamp(dand.health + 1, 0, 100);
  }

  if (light < 9.0f) {
    dand.health -= 1;
  }

  if (dand.health <= 0) {
    return 2;
  }
  return rc;
}

GridCoords gen_seed(std::mt19937 &mt) {
  int dist = std::round(wind_dist_dist(mt) + 3.0f * (float)wind_speed / 3.6f);
  int angle = std::round(wind_angle_dist_normal(mt)) + wind_dir;
  int movex = std::round(dist * std::sin(angle * DEG2RAD));
  int movey = std::round(dist * std::cos(angle * DEG2RAD));
  return {movex, movey};
}

void worker1() {
  while (true) {
    std::unique_lock lk(grid1_mutex);
    cv1.wait(lk, [] { return cv1_state == 1; });
    if (should_close) {
      break;
    }
    std::deque<std::vector<Dandelion>::iterator> death_queue;
    std::queue<std::vector<Dandelion>::iterator> puff_queue;
    for (int y = 0; y < 50; ++y) {
      for (int x = 0; x < 50; ++x) {
        auto &vec = grid1[y][x];
        for (auto i = vec.begin(); i < vec.end(); ++i) {
          int rc = handle_dandelion(*i, mtrandom1);
          if (rc == 2) {
            death_queue.push_back(i);
          } else if (rc == 1) {
            puff_queue.push(i);
          }
        }
        while (puff_queue.size() > 0) {
          auto i = puff_queue.front();
          puff_queue.pop();
          int seeds = seeds_dist(mtrandom1);
          if (i->is_first) {
            seeds /= ratio;
          }
          for (int j = 0; j < seeds; ++j) {
            GridCoords seed = gen_seed(mtrandom1);
            GridCoords new_coords = {x + seed.x, y - seed.y};
            if (new_coords.x < 0 || new_coords.y < 0 || new_coords.x > 99 ||
                new_coords.y > 99) {
              continue;
            }
            grid1_seed_queue.push({new_coords, Dandelion(mtrandom1)});
            full_grid[new_coords.y][new_coords.x] += ratio;
            total_dandelion_number += ratio;
          }
        }
        while (death_queue.size() > 0) {
          auto i = death_queue.back();
          death_queue.pop_back();
          vec.erase(i);
          full_grid[y][x] -= ratio;
          total_dandelion_number -= ratio;
        }
      }
    }
    cv1_state = 0;
    lk.unlock();
    cv1.notify_one();
  }
}
void worker2() {
  while (true) {
    std::unique_lock lk(grid2_mutex);
    cv2.wait(lk, [] { return cv2_state == 1; });
    if (should_close) {
      break;
    }
    std::deque<std::vector<Dandelion>::iterator> death_queue;
    std::queue<std::vector<Dandelion>::iterator> puff_queue;
    for (int y = 0; y < 50; ++y) {
      for (int x = 0; x < 50; ++x) {
        auto &vec = grid2[y][x];
        for (auto i = vec.begin(); i < vec.end(); ++i) {
          int rc = handle_dandelion(*i, mtrandom2);
          if (rc == 2) {
            death_queue.push_back(i);
          } else if (rc == 1) {
            puff_queue.push(i);
          }
        }
        while (puff_queue.size() > 0) {
          auto i = puff_queue.front();
          puff_queue.pop();
          int seeds = seeds_dist(mtrandom2);
          if (i->is_first) {
            seeds /= ratio;
          }
          for (int j = 0; j < seeds; ++j) {
            GridCoords seed = gen_seed(mtrandom2);
            GridCoords new_coords = {x + 50 + seed.x, y - seed.y};
            if (new_coords.x < 0 || new_coords.y < 0 || new_coords.x > 99 ||
                new_coords.y > 99) {
              continue;
            }
            grid2_seed_queue.push({new_coords, Dandelion(mtrandom2)});
            full_grid[new_coords.y][new_coords.x] += ratio;
            total_dandelion_number += ratio;
          }
        }
        while (death_queue.size() > 0) {
          auto i = death_queue.back();
          death_queue.pop_back();
          vec.erase(i);
          full_grid[y][x + 50] -= ratio;
          total_dandelion_number -= ratio;
        }
      }
    }
    cv2_state = 0;
    lk.unlock();
    cv2.notify_one();
  }
}
void worker3() {
  while (true) {
    std::unique_lock lk(grid3_mutex);
    cv3.wait(lk, [] { return cv3_state == 1; });
    if (should_close) {
      break;
    }
    std::deque<std::vector<Dandelion>::iterator> death_queue;
    std::queue<std::vector<Dandelion>::iterator> puff_queue;
    for (int y = 0; y < 50; ++y) {
      for (int x = 0; x < 50; ++x) {
        auto &vec = grid3[y][x];
        for (auto i = vec.begin(); i < vec.end(); ++i) {
          int rc = handle_dandelion(*i, mtrandom3);
          if (rc == 2) {
            death_queue.push_back(i);
          } else if (rc == 1) {
            puff_queue.push(i);
          }
        }
        while (puff_queue.size() > 0) {
          auto i = puff_queue.front();
          puff_queue.pop();
          int seeds = seeds_dist(mtrandom3);
          if (i->is_first) {
            seeds /= ratio;
          }
          for (int j = 0; j < seeds; ++j) {
            GridCoords seed = gen_seed(mtrandom3);
            GridCoords new_coords = {x + seed.x, y + 50 - seed.y};
            if (new_coords.x < 0 || new_coords.y < 0 || new_coords.x > 99 ||
                new_coords.y > 99) {
              continue;
            }
            grid3_seed_queue.push({new_coords, Dandelion(mtrandom3)});
            full_grid[new_coords.y][new_coords.x] += ratio;
            total_dandelion_number += ratio;
          }
        }
        while (death_queue.size() > 0) {
          auto i = death_queue.back();
          death_queue.pop_back();
          vec.erase(i);
          full_grid[y + 50][x] -= ratio;
          total_dandelion_number -= ratio;
        }
      }
    }
    cv3_state = 0;
    lk.unlock();
    cv3.notify_one();
  }
}
void worker4() {
  while (true) {
    std::unique_lock lk(grid4_mutex);
    cv4.wait(lk, [] { return cv4_state == 1; });
    if (should_close) {
      break;
    }
    std::deque<std::vector<Dandelion>::iterator> death_queue;
    std::queue<std::vector<Dandelion>::iterator> puff_queue;
    for (int y = 0; y < 50; ++y) {
      for (int x = 0; x < 50; ++x) {
        auto &vec = grid4[y][x];
        for (auto i = vec.begin(); i < vec.end(); ++i) {
          int rc = handle_dandelion(*i, mtrandom4);
          if (rc == 2) {
            death_queue.push_back(i);
          } else if (rc == 1) {
            puff_queue.push(i);
          }
        }
        while (puff_queue.size() > 0) {
          auto i = puff_queue.front();
          puff_queue.pop();
          int seeds = seeds_dist(mtrandom4);
          if (i->is_first) {
            seeds /= ratio;
          }
          for (int j = 0; j < seeds; ++j) {
            GridCoords seed = gen_seed(mtrandom4);
            GridCoords new_coords = {x + 50 + seed.x, y + 50 - seed.y};
            if (new_coords.x < 0 || new_coords.y < 0 || new_coords.x > 99 ||
                new_coords.y > 99) {
              continue;
            }
            grid4_seed_queue.push({new_coords, Dandelion(mtrandom4)});
            full_grid[new_coords.y][new_coords.x] += ratio;
            total_dandelion_number += ratio;
          }
        }
        while (death_queue.size() > 0) {
          auto i = death_queue.back();
          death_queue.pop_back();
          vec.erase(i);
          full_grid[y + 50][x + 50] -= ratio;
          total_dandelion_number -= ratio;
        }
      }
    }
    cv4_state = 0;
    lk.unlock();
    cv4.notify_one();
  }
}

void handle_seed_queue(std::queue<NewSeed> &seed_queue) {
  while (seed_queue.size() > 0) {
    NewSeed seed = seed_queue.front();
    seed_queue.pop();
    int x = seed.coords.x;
    int y = seed.coords.y;
    if (x < 0 || y < 0 || x > 99 || y > 99) {
      continue;
    }
    if (x <= 49 && y <= 49) {
      grid1[y][x].push_back(seed.dandelion);
    } else if (x > 49 && y <= 49) {
      grid2[y][x - 50].push_back(seed.dandelion);
    } else if (x <= 49 && y > 49) {
      grid3[y - 50][x].push_back(seed.dandelion);
    } else {
      grid4[y - 50][x - 50].push_back(seed.dandelion);
    }
  }
}

void simulate_master() {
  std::thread worker1_thread(worker1);
  std::thread worker2_thread(worker2);
  std::thread worker3_thread(worker3);
  std::thread worker4_thread(worker4);

  auto last_frame = std::chrono::high_resolution_clock::now();

  while (!should_close) {
    if (!paused) {
      for (const auto &s : snap_dates) {
        if (date == s) {
          std::string image_filename = date + ".png";
          std::cout << "Saving " << image_filename << "..." << std::endl;
          unsigned char *image = (unsigned char *)std::malloc(800 * 800 * 3);
          for (int i = 0 ; i < 800 * 800 * 3; ++i) {
            image[i] = 255;
          }
          for (int y = 0; y < 800; ++y) {
            for (int x = 0; x < 800; ++x) {
              int gridy = y / 8;
              int gridx = x / 8;
              int size = full_grid[gridy][gridx];
              if (size > 0) {
                int diff = clamp(((size / 100) + 1) * 10, 0, 240);
                unsigned char val = (unsigned char)(255 - diff);
                image[y * 800 * 3 + x * 3] = val;
                image[y * 800 * 3 + x * 3 + 1] = val;
                image[y * 800 * 3 + x * 3 + 2] = val;
              }
            }
          }
          stbi_write_png(image_filename.c_str(), 800, 800, 3, image, 800 * 3);
          free(image);
          std::string text_filename = date + ".txt";
          std::cout << "Saving " << text_filename << "..." << std::endl;
          std::ofstream text_file(text_filename);
          for (int y = 0; y < segments; ++y) {
            for (int x = 0; x < segments; ++x) {
              text_file << full_grid[y][x] << ' ';
            }
            text_file << '\n';
          }
        }
      }
      if (!reader->read_row(date, ntemperature, nprecipitation, nwind_dir,
                            nwind_speed)) {
        std::cout << "Real world data ran out! Pausing." << std::endl;
        paused = true;
        continue;
      }
      temperature = ntemperature;
      precipitation = nprecipitation;
      wind_dir = nwind_dir;
      wind_speed = nwind_speed;
      int month = (date[5] - 48) * 10 + (date[6] - 48);
      if (month == 12 || month == 1 || month == 2) {
        season = Season::Winter;
      } else if (month >= 3 && month <= 5) {
        season = Season::Spring;
      } else if (month >= 6 && month <= 8) {
        season = Season::Summer;
      } else {
        season = Season::Autumn;
      }
      humidity = humidities[(int)season.load()][(int)climate.load()];
      light = lights[(int)season.load()][(int)climate.load()];

      cv1_state = 1;
      cv2_state = 1;
      cv3_state = 1;
      cv4_state = 1;
      cv1.notify_one();
      cv2.notify_one();
      cv3.notify_one();
      cv4.notify_one();

      std::unique_lock lk1(grid1_mutex);
      cv1.wait(lk1, [] { return cv1_state == 0; });
      std::unique_lock lk2(grid2_mutex);
      cv2.wait(lk2, [] { return cv2_state == 0; });
      std::unique_lock lk3(grid3_mutex);
      cv3.wait(lk3, [] { return cv3_state == 0; });
      std::unique_lock lk4(grid4_mutex);
      cv4.wait(lk4, [] { return cv4_state == 0; });

      handle_seed_queue(grid1_seed_queue);
      handle_seed_queue(grid2_seed_queue);
      handle_seed_queue(grid3_seed_queue);
      handle_seed_queue(grid4_seed_queue);

      lk1.unlock();
      lk2.unlock();
      lk3.unlock();
      lk4.unlock();

      auto dur = std::chrono::high_resolution_clock::now() - last_frame;
      std::this_thread::sleep_for(
          std::chrono::duration<float, std::milli>(day_length) - dur);

      auto current_frame = std::chrono::high_resolution_clock::now();
      last_frame = current_frame;

      day++;
    }
  }
  cv1_state = 1;
  cv2_state = 1;
  cv3_state = 1;
  cv4_state = 1;
  cv1.notify_one();
  cv2.notify_one();
  cv3.notify_one();
  cv4.notify_one();

  worker1_thread.join();
  worker2_thread.join();
  worker3_thread.join();
  worker4_thread.join();
}

Vector2 transform_point(Vector2 in) {
  return {(float)((zoom * (in.x - camera.x)) + view_width / 2.0f),
          (float)(-(zoom * (in.y - camera.y)) + view_height / 2.0f +
                  (float)top_bar_height)};
}

Vector2 transform_size(Vector2 in) {
  return {(float)(zoom * in.x), (float)(zoom * in.y)};
}

int main(int argc, char **argv) {
  if (argc < 4) {
    std::cout << "usage: " << argv[0]
              << " <filename> [polar|continental|tropical|desert|temperate] "
                 "<ratio> snap_dates"
              << std::endl;
    return 0;
  }

  std::random_device real_random;

  mtrandom = std::mt19937(real_random());
  mtrandom1 = std::mt19937(real_random());
  mtrandom2 = std::mt19937(real_random());
  mtrandom3 = std::mt19937(real_random());
  mtrandom4 = std::mt19937(real_random());

  io::CSVReader<5> data_reader(argv[1]);
  data_reader.read_header(io::ignore_extra_column | io::ignore_missing_column,
                          "date", "tavg", "prcp", "wdir", "wspd");
  reader = &data_reader;
  if (std::strcmp(argv[2], "polar") == 0) {
    climate = Climate::Polar;
  } else if (std::strcmp(argv[2], "continental") == 0) {
    climate = Climate::Continental;
  } else if (std::strcmp(argv[2], "tropical") == 0) {
    climate = Climate::Tropical;
  } else if (std::strcmp(argv[2], "desert") == 0) {
    climate = Climate::Desert;
  } else {
    climate = Climate::Temperate;
  }
  int rat = std::stoi(argv[3]);
  if (rat > 0) {
    ratio = rat;
  }
  snap_dates.reserve(argc - 4);
  for (int i = 4; i < argc; ++i) {
    snap_dates.push_back(argv[i]);
  }

  // FIRST DANDELION
  Dandelion first_dandelion = Dandelion(mtrandom);
  first_dandelion.age =
      first_dandelion.germination_time + first_dandelion.mature_time +
      first_dandelion.flower_time + first_dandelion.wither_time +
      first_dandelion.puffball_time;
  first_dandelion.days_since_last_stage = first_dandelion.puffball_time;
  first_dandelion.stage = Dandelion::Stage::Puffball;
  first_dandelion.is_first = true;
  grid1[49][49].push_back(first_dandelion);
  full_grid[49][49]++;
  total_dandelion_number++;

  SetConfigFlags(FLAG_MSAA_4X_HINT);
  InitWindow(win_width, win_height, "dandelion");
  SetTargetFPS(60);

  std::thread master_thread(simulate_master);

  Vector2 mouse_position;
  bool dragging = false;
  GridCoords selected = {-1, -1};

  while (!WindowShouldClose()) {
    mouse_position = GetMousePosition();

    if (IsKeyDown(KEY_RIGHT))
      camera.x += 2.0f;
    if (IsKeyDown(KEY_LEFT))
      camera.x -= 2.0f;
    if (IsKeyDown(KEY_UP))
      camera.y += 2.0f;
    if (IsKeyDown(KEY_DOWN))
      camera.y -= 2.0f;
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
      Vector2 mouse_delta = GetMouseDelta();
      if (mouse_delta.x != 0 && mouse_delta.y != 0) {
        mouse_delta.x /= zoom;
        mouse_delta.y /= zoom;
        camera.x -= mouse_delta.x;
        camera.y += mouse_delta.y;
        dragging = true;
      }
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
      if (!dragging) {
        bool found = false;
        for (int y = 0; y < segments; ++y) {
          for (int x = 0; x < segments; ++x) {
            float block_size = zoom * 8.0f;
            Vector2 top_left = {(float)((x * block_size) - 400),
                                (float)(400 - (y * block_size))};
            top_left = transform_point(top_left);
            if (CheckCollisionPointRec(
                    mouse_position,
                    {top_left.x, top_left.y, block_size, block_size})) {
              selected = {x, y};
              found = true;
              break;
            }
          }
          if (found) {
            break;
          }
        }
        if (!found) {
          selected = {-1, -1};
        }
      }
      dragging = false;
    }

    if (IsKeyDown(KEY_EQUAL))
      zoom *= zoom_mult;
    if (IsKeyDown(KEY_MINUS))
      zoom /= zoom_mult;
    if (CheckCollisionPointRec(mouse_position,
                               {0, top_bar_height, view_width, view_height})) {
      float wheel_move = GetMouseWheelMove();
      if (wheel_move > 0) {
        zoom *= wheel_move * zoom_mult * 1.01f;
      } else if (wheel_move < 0) {
        zoom /= std::abs(wheel_move) * zoom_mult * 1.01f;
      }
    }

    if (IsKeyReleased(KEY_SPACE))
      paused = !paused;

    BeginDrawing();
    ClearBackground(RAYWHITE);

    // VIEWPORT
    Vector2 top_left = {-(view_width / 2.0f), (view_height / 2.0f)};
    Vector2 size = {view_width, view_height};
    DrawRectangleV(transform_point(top_left), transform_size(size), WHITE);

    for (int y = 0; y < segments; ++y) {
      for (int x = 0; x < segments; ++x) {
        bool s = false;
        if (selected.x == x && selected.y == y) {
          s = true;
        }
        int size = full_grid[y][x];
        Vector2 top_left = {(float)(x * 8 - 400), (float)(400 - y * 8)};
        if (size > 0) {
          int diff = clamp(((size / 100) + 1) * 10, 0, 240);
          unsigned char val = (unsigned char)(255 - diff);
          Color c = {val, val, val, 255};
          DrawRectangleV(transform_point(top_left),
                         transform_size({8.0f, 8.0f}), c);
        }
        if (s) {
          top_left = transform_point(top_left);
          DrawRectangleLines(top_left.x, top_left.y, zoom * 8.0f, zoom * 8.0f,
                             GREEN);
        }
      }
    }

    // TOP BAR
    DrawRectangle(0, 0, view_width, top_bar_height, RAYWHITE);
    DrawLine(0, top_bar_height, view_width, top_bar_height, BLACK);

    std::string status_text = fmt::format(
        "{} | {:.1f} C | {}% | {:.2f} h | {:.1f} | {} | {:.2f}",
        season_strings[(int)season.load()], temperature.load(), humidity.load(),
        light.load(), precipitation.load(), wind_dir.load(), wind_speed.load());
    DrawText(status_text.c_str(), 10, 10, 20, BLACK);

    if (selected.x != -1 && selected.y != -1) {
      std::string num_text =
          fmt::format("{}", full_grid[selected.y][selected.x].load());
      int num_text_width = MeasureText(num_text.c_str(), 20);
      DrawText(num_text.c_str(), view_width - 10 - num_text_width, 10, 20,
               BLACK);
    }

    // BOTTOM BAR
    DrawRectangle(0, top_bar_height + view_height, view_width,
                  bottom_bar_height, RAYWHITE);
    DrawLine(0, top_bar_height + view_height, view_width,
             top_bar_height + view_height, BLACK);

    Rectangle pause_button_rec = {10, top_bar_height + view_height + 10, 20,
                                  20};
    if (paused) {
      if (GuiButton(pause_button_rec, "#131#")) {
        paused = false;
      }
    } else {
      if (GuiButton(pause_button_rec, "#132#")) {
        paused = true;
      }
    }
    DrawText(fmt::format("Day {}", day.load()).c_str(), 40,
             top_bar_height + view_height + 10, 20, BLACK);

    Rectangle zoom_in_rec = {view_width - 30, top_bar_height + view_height + 10,
                             20, 20};
    if (GuiButton(zoom_in_rec, "#220#")) {
      zoom *= zoom_mult;
    }
    Rectangle zoom_out_rec = {view_width - 60,
                              top_bar_height + view_height + 10, 20, 20};
    if (GuiButton(zoom_out_rec, "#221#")) {
      zoom /= zoom_mult;
    }
    std::string zoom_text = fmt::format("Zoom: {}%", std::round(zoom * 100.0));
    int zoom_text_width = MeasureText(zoom_text.c_str(), 20);
    DrawText(zoom_text.c_str(), win_width - 70 - zoom_text_width,
             top_bar_height + view_height + 10, 20, BLACK);

    std::string total_text = fmt::format("{}", total_dandelion_number.load());
    int total_text_width = MeasureText(total_text.c_str(), 20);
    DrawText(total_text.c_str(), (view_width - total_text_width) / 2,
             top_bar_height + view_height + 10, 20, BLACK);

    EndDrawing();
  }
  should_close = true;
  master_thread.join();
  CloseWindow();
  return 0;
}