#include <cassert>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <chrono>
#include <list>
#include <set>
#include <functional>
#include <utility>
#include <string>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>



const int window_width = 1920;
const int window_height = 1080;

const int NODE_TYPE_GROUND_TRUTH = 0;
const int NODE_TYPE_INPUT = 1;


int64_t time_epsilon = 100;


struct Node {
  int type;
  int64_t time;
  int64_t midi_value;
  int64_t velocity;
  const Node *matched = nullptr;

  bool operator<(const Node &rhs) {
    return this->time < rhs.time;
  }
};


void match_nodes(
    std::list<Node> &nodes,
    int64_t current_time,
    const std::function<void(bool matched, const Node *node)> &callback) {

  for (auto it = nodes.begin(); it != nodes.end(); it++) {
    auto &node = *it;

    if (node.matched != nullptr) {
      continue;
    }

    if (node.time < current_time - time_epsilon) {
      // matched node determined
      auto it2 = it;
      it2++;
      for (; it2 != nodes.end(); it2++) {
        auto &node2 = *it2;
        if (node2.time - node.time >= time_epsilon) {
          break;
        }

        if (node.midi_value == node2.midi_value &&
            node.type != node2.type &&
            node.matched == nullptr &&
            node2.matched == nullptr) {

          node.matched = &node2;
          node2.matched = &node;
          break;
        }
      }
    }
  }

  for (const auto &node : nodes) {
    if (node.matched != nullptr && node.type == NODE_TYPE_GROUND_TRUTH) {
      callback(true, &node);
    }

    if (node.matched == nullptr && node.time < current_time - time_epsilon) {
      callback(false, &node);
    }
  }

  nodes.erase(
      std::remove_if(nodes.begin(), nodes.end(), [current_time](const Node &node) -> bool {
        return node.time < current_time - time_epsilon || node.matched != nullptr;
      }),
      nodes.end()
  );
}


void draw_text(SDL_Renderer *renderer, TTF_Font *font, const std::string &text, const SDL_Color *color, int x, int y) {
  if (text.empty()) {
    return;
  }
  SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), *color); // as TTF_RenderText_Solid could only be used on SDL_Surface then you have to create the surface first
  SDL_Texture* message = SDL_CreateTextureFromSurface(renderer, surface); //now you can convert it into a texture
  SDL_Rect message_rect; //create a rect
  message_rect.x = x;
  message_rect.y = y;
  message_rect.w = surface->w;
  message_rect.h = surface->h;

  SDL_FreeSurface(surface);
  SDL_RenderCopy(renderer, message, NULL, &message_rect); //you put the renderer's name first, the Message, the crop size(you can ignore this if you don't want to dabble with cropping), and the rect which is the size and coordinate of your texture
  SDL_DestroyTexture(message);
}

class SDLConsole {
 public:
  SDLConsole(SDL_Renderer *renderer, int x, int y, size_t max_lines = 12) :renderer(renderer), x(x), y(y), color(), max_lines(max_lines) {
    font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSansMono.ttf", 48); //this opens a font style and sets a size
    if (font == nullptr) {
      std::cerr << "TTF_OpenFont: " << TTF_GetError() << std::endl;
      exit(1);
    }
    SDL_Color text_color = {255, 255, 255};  // this is the color in rgb format, maxing out all would give you the color white, and it will be your text's color
    this->color = text_color;

  }

  void print(const std::string &line) {
    lines.push_front(line);
  }
  void render() {
    update();
  }
 private:
  void update() {
    int i = 0;
    for (const auto &line : lines) {
      if (i >= this->max_lines) {
        break;
      }
      if (!line.empty()) {
        SDL_Surface* surface = TTF_RenderText_Solid(font, line.c_str(), color); // as TTF_RenderText_Solid could only be used on SDL_Surface then you have to create the surface first
        SDL_Texture* message = SDL_CreateTextureFromSurface(renderer, surface); //now you can convert it into a texture
        SDL_Rect message_rect; //create a rect
        message_rect.x = x;
        message_rect.y = this->y + i * line_height;
        message_rect.w = surface->w;
        message_rect.h = surface->h;
        SDL_FreeSurface(surface);
        SDL_RenderCopy(renderer, message, NULL, &message_rect); //you put the renderer's name first, the Message, the crop size(you can ignore this if you don't want to dabble with cropping), and the rect which is the size and coordinate of your texture
        SDL_DestroyTexture(message);
      }
      i++;
    }
  }
 private:
  SDL_Renderer *renderer;
  TTF_Font *font;
  SDL_Color color;
  int x, y;
  std::list<std::string> lines;
  int line_height = 50;
  int max_lines;
};

#include "MidiFile.h"
int main() {

  smf::MidiFile midifile;
  midifile.read("test.midi");
  midifile.doTimeAnalysis();
  midifile.linkNotePairs();

  std::list<Node> ground_truth;

  for (int track = 0; track < midifile.getTrackCount(); track++) {
    for (int event = 0; event < midifile[track].size(); event++) {
      if (midifile[track][event].isNoteOn()) {
        int ms = midifile[track][event].seconds * 1000;
        for (int i=0; i<midifile[track][event].size(); i++) {
          int velocity = midifile[track][event].getVelocity();
          int midi_value = midifile[track][event][i];
          Node node{0, ms, midi_value, velocity};
          ground_truth.push_back(node);
        }
      }
    }
  }
  ground_truth.sort();

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "could not initialize sdl2: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window *window = SDL_CreateWindow(
      "Mandelbrot Set",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      window_width, window_height,
      SDL_WINDOW_SHOWN
  );
  if (window == NULL) {
    fprintf(stderr, "could not create window: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  SDL_Event event;
  int buffer_id = 0;
  auto t0 = std::chrono::high_resolution_clock::now();
  auto last_t = std::chrono::high_resolution_clock::now();

  if (0 != TTF_Init()) {
    std::cerr << "TTF_Init: " << TTF_GetError() << std::endl;
    exit(1);
  }
  TTF_Font* font = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSansMono.ttf", 48); //this opens a font style and sets a size
  if (font == nullptr) {
    std::cerr << "TTF_OpenFont: " << TTF_GetError() << std::endl;
    exit(1);
  }
  SDL_Color text_color = {255, 255, 255};  // this is the color in rgb format, maxing out all would give you the color white, and it will be your text's color

  SDLConsole console(renderer, 20, 500);

  std::list<Node> pending_nodes;
  while (true) {
    auto now = std::chrono::high_resolution_clock::now();
    auto delta_t = std::chrono::duration_cast<std::chrono::duration<int64_t, std::milli>>(now - t0);
    int64_t current_time = delta_t.count();

    SDL_PollEvent(&event);
    std::list<Node> input_nodes;
    if (event.type == SDL_KEYDOWN) {
      if (event.key.keysym.sym == SDLK_ESCAPE) {
        break;
      }
      if (event.key.keysym.scancode >= SDL_SCANCODE_A && event.key.keysym.scancode <= SDL_SCANCODE_Z) {
        int ch = 'a' + (event.key.keysym.scancode - SDL_SCANCODE_A);

        input_nodes.push_back({1, current_time, ch, 1});

//        {
//          std::stringstream ss;
//          ss << ": " << std::chrono::duration_cast<std::chrono::duration<int64_t, std::milli>>(now - t0).count();
//          ss << " key down " << (char)ch;
//          console.print(ss.str());
//        }
      }
    }


    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);


    // merge sort `nodes` and `input_nodes`
    while (!ground_truth.empty() && !input_nodes.empty()) {
      if (ground_truth.front().time < input_nodes.front().time) {
        pending_nodes.push_back(ground_truth.front());
        ground_truth.pop_front();
      } else {
        pending_nodes.push_back(input_nodes.front());
        input_nodes.pop_front();
      }
    }

    for (auto &input_node : input_nodes) {
      pending_nodes.push_back(input_node);
    }
    while (!ground_truth.empty()) {
      if (ground_truth.front().time > current_time) {
        break;
      } else {
        pending_nodes.push_back(ground_truth.front());
        ground_truth.pop_front();
      }
    }

    match_nodes(pending_nodes, current_time, [&console](bool matched, const Node *node) {
      if (!matched) {
        std::stringstream ss;
        ss << (node->type == NODE_TYPE_GROUND_TRUTH ? "+ " : "- ") << std::setfill('0') << std::setw(4) << node->time << " "
                  << (node->type == NODE_TYPE_GROUND_TRUTH ? "miss" : "wrong") << " " << node->midi_value;

        console.print(ss.str());
      } else {
        assert(node->matched != nullptr);
        {
          std::stringstream ss;
          ss << "= " << std::setfill('0') << std::setw(4) << node->time << "(gt) "
             << "matched " << std::setfill('0') << std::setw(4) << node->matched->time;
          console.print(ss.str());
        }
      }
    });


    console.render();

    {
      std::stringstream ss;
      ss << current_time;
      draw_text(renderer, font, ss.str(), &text_color, 0, 0);
    }
    {
      double fps = 1.0 / std::chrono::duration<float>(now - last_t).count();
      std::stringstream ss;
      ss << std::fixed << std::setw(6) << std::setprecision(2) << std::setfill('0') << fps;
      draw_text(renderer, font, ss.str(), &text_color, window_width-200, 0);
      last_t = now;
    }


    SDL_RenderPresent(renderer);
  }

  SDL_DestroyWindow(window);
  SDL_Quit();
}
