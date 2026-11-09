// Interactive 3D Rubik's cube GUI — INTEGRATION track.
//
// Wires Facelet + Renderer + 5 solvers into ImGui.
// Key exports (tests/visualiser/test_integration.cpp mirrors these):
//   concat_stages(const Solution&) / solver_registry() /
//   replay_solves(CubeState, Solution) / is_unset / count_unset /
//   solve_error_name / to_array / to_string54 / solved_facelets /
//   scramble_facelets
// (overlay_copy removed: highlight is now Renderer::setHighlighted state.)
// Highlighting: Renderer stores highlighted indices (setHighlighted /
// highlighted) and drawQuads tints them yellow; GuiState.highlight feeds
// it each frame. Window via Renderer::window() (no private hack).
#include "visualiser/renderer.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "core/cube.hpp"
#include "core/interface.hpp"
#include "core/moves.hpp"
#include "solvers/cfop/solver.hpp"
#include "solvers/heise/solver.hpp"
#include "solvers/roux/solver.hpp"
#include "solvers/twophase/solver.hpp"
#include "solvers/zz/solver.hpp"
#include "visualiser/facelet.hpp"

using namespace rubik;
using namespace rubik::visualiser;

// ---------- shared helpers (test mirrors these) ----------
inline std::vector<std::string> concat_stages(const Solution& s) {
  std::vector<std::string> out;
  for (const auto& st : s.stages)
    for (const auto& m : st.moves) out.push_back(m);
  return out;
}
struct SolverEntry { const char* name; SolverFn fn; };
inline std::vector<SolverEntry> solver_registry() {
  return {{"CFOP", &rubik::cfop::solve}, {"Roux", &rubik::roux::solve},
          {"ZZ", &rubik::zz::solve}, {"Heise", &rubik::heise::solve},
          {"Twophase", &rubik::twophase::solve}};
}
inline CubeState replay_solves(CubeState start, const Solution& sol) {
  CubeState s = start;
  for (const auto& tok : concat_stages(sol)) s = applyTokenInstant(s, tok);
  return s;
}
inline const char* solve_error_name(SolveError e) {
  switch (e) {
    case SolveError::Unsolvable: return "Unsolvable";
    case SolveError::Timeout: return "Timeout";
    case SolveError::Cancelled: return "Cancelled";
    case SolveError::DatabaseMissing: return "DatabaseMissing";
    case SolveError::InvalidInput: return "InvalidInput";
    case SolveError::NotImplemented: return "NotImplemented";
  }
  return "?";
}
inline bool is_unset(char c) { return c == '?' || c == ' ' || c == '\0'; }
inline int count_unset(const FaceletArray& f) {
  int n = 0;
  for (char c : f) if (is_unset(c)) ++n;
  return n;
}
inline FaceletArray to_array(const std::string& s) {
  FaceletArray a{};
  std::copy_n(s.begin(), std::min<std::size_t>(54, s.size()), a.begin());
  if (s.size() < 54) std::fill(a.begin() + s.size(), a.end(), '?');
  return a;
}
inline std::string to_string54(const FaceletArray& a) { return {a.begin(), a.end()}; }
inline FaceletArray solved_facelets() { return cube_to_facelets(CubeState::solved()); }
inline FaceletArray scramble_facelets(int length = 20, std::uint32_t seed = 0) {
  static std::mt19937 rng(seed ? seed : std::random_device{}());
  static const char* faces[] = {"U", "R", "F", "D", "L", "B"};
  CubeState s = CubeState::solved();
  for (int i = 0; i < length; ++i) {
    std::uniform_int_distribution<int> dface(0, 5), dsuf(0, 2);
    std::string tok = faces[dface(rng)];
    int suf = dsuf(rng);
    if (suf == 1) tok += '\''; else if (suf == 2) tok += '2';
    s = apply(s, Move{tok});
  }
  return cube_to_facelets(s);
}

// ---------- GUI state ----------
struct GuiState {
  FaceletArray display{};
  AnimatedCube animated;
  bool replay_active = false;
  int selected_method = 4;
  int selected_color = 0;  // 0..5 palette, 6 eraser
  std::vector<int> highlight;
  std::string status = "Ready";
  std::thread worker;
  std::stop_source stop_src;
  std::atomic<bool> solving{false};
  struct Result {
    bool done = false;
    std::expected<Solution, SolveError> res = std::unexpected(SolveError::NotImplemented);
    std::string method;
    FaceletArray start_facelets{};
  };
  std::mutex mu;
  Result result;
  CubeState solve_start;
  bool has_solved = false;
};
static ImVec4 facelet_color(char c) {
  switch (c) {
    case 'U': return {0.95f,0.95f,0.95f,1}; case 'R': return {0.85f,0.10f,0.10f,1};
    case 'F': return {0.10f,0.65f,0.20f,1}; case 'D': return {0.95f,0.85f,0.10f,1};
    case 'L': return {1.00f,0.55f,0.05f,1}; case 'B': return {0.10f,0.25f,0.95f,1};
  }
  return {0.35f,0.35f,0.35f,1};
}
static void draw_net(GuiState& g) {
  ImGui::Separator();
  ImGui::TextUnformatted("Facelet net (click stickers in 3D, or buttons)");
  char names[6] = {'U','R','F','D','L','B'};
  for (int f = 0; f < 6; ++f) {
    ImGui::SameLine(0, 18);
    ImGui::BeginGroup();
    ImGui::Text("%c", names[f]);
    for (int i = 0; i < 9; ++i) {
      int idx = f*9+i;
      char c = g.display[(std::size_t)idx];
      ImGui::PushStyleColor(ImGuiCol_Button, facelet_color(c));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, facelet_color(c));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, facelet_color(c));
      char label[2] = {c, '\0'};
      if (ImGui::SmallButton(label)) {
        if (g.selected_color < 6) g.display[(std::size_t)idx] = names[g.selected_color];
        else g.display[(std::size_t)idx] = '?';
        g.highlight.clear();
      }
      ImGui::PopStyleColor(3);
      if ((i%3)!=2) ImGui::SameLine(0,2);
    }
    ImGui::EndGroup();
  }
  ImGui::Text("Unset: %d / 54", count_unset(g.display));
}
static void draw_palette(GuiState& g) {
  ImGui::Separator();
  ImGui::TextUnformatted("Palette");
  char names[6] = {'U','R','F','D','L','B'};
  const char* labels[7] = {"U white","R red","F green","D yellow","L orange","B blue","Eraser"};
  for (int i = 0; i < 7; ++i) {
    if (i) ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, i<6?facelet_color(names[i]):ImVec4{0.25f,0.25f,0.25f,1});
    if (g.selected_color==i) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{1,1,0,1});
    if (ImGui::Button(labels[i], ImVec2(80,24))) g.selected_color = i;
    ImGui::PopStyleColor(g.selected_color==i?2:1);
  }
}
static void poll_solve_result(GuiState& g) {
  GuiState::Result snap;
  { std::lock_guard<std::mutex> lk(g.mu);
    if (!g.result.done) return;
    snap = std::move(g.result); g.result.done = false; }
  if (snap.res) {
    auto tokens = concat_stages(*snap.res);
    g.animated.setState(snap.start_facelets);
    g.animated.setMoves(tokens);
    g.replay_active = true;
    CubeState end = replay_solves(g.solve_start, *snap.res);
    if (is_solved(end) && cube_to_facelets(end)==cube_to_facelets(CubeState::solved()))
      { g.status = "Solved with "+snap.method+": "+std::to_string(snap.res->total_moves())+" moves (verified)"; g.has_solved = true; }
    else g.status = "ERROR: replay did not reach solved";
  } else {
    g.status = std::string("Solve failed: ")+solve_error_name(snap.res.error())+" ("+snap.method+")";
    if (snap.res.error()==SolveError::DatabaseMissing) g.status += " — tables missing";
  }
}
// Handle Solve click; returns immediately, thread computes.
static void on_solve_click(GuiState& g) {
  if (count_unset(g.display) > 0) {
    g.status = "Cannot solve: "+std::to_string(count_unset(g.display))+" unset stickers";
    g.highlight.clear(); return;
  }
  auto v = validate_facelets(g.display);
  if (v.error != FaceletError::Ok) {
    g.status = std::string("Cannot solve — invalid: ")+to_string(v.error)+" — "+v.message;
    g.highlight = v.offending; return;
  }
  auto cube_res = facelets_to_cube(g.display);
  if (!cube_res) { g.status = "Cannot solve — conversion failed"; g.highlight = cube_res.error().offending; return; }
  auto registry = solver_registry();
  SolverFn fn = registry[(std::size_t)g.selected_method].fn;
  std::string method = registry[(std::size_t)g.selected_method].name;
  g.solve_start = *cube_res;
  g.stop_src = std::stop_source{};
  g.solving.store(true);
  g.status = "Solving...";
  if (g.worker.joinable()) g.worker.join();
  CubeState start = g.solve_start; std::stop_token tok = g.stop_src.get_token();
  FaceletArray start_f = g.display;
  { std::lock_guard<std::mutex> lk(g.mu); g.result = GuiState::Result{}; g.result.start_facelets = start_f; }
  g.worker = std::thread([fn, start, tok, method, &g] {
    SolveOptions opts{60000, 20, 4, tok};
    auto res = fn(start, opts);
    std::lock_guard<std::mutex> lk(g.mu);
    g.result.done = true; g.result.res = std::move(res); g.result.method = method;
    g.solving.store(false);
  });
}
static void draw_controls(GuiState& g) {
  ImGui::Separator();
  auto registry = solver_registry();
  const char* items[5];
  for (int i=0;i<5;++i) items[i] = registry[(std::size_t)i].name;
  ImGui::Combo("Method", &g.selected_method, items, 5);
  if (ImGui::Button("Reset (solved)")) { g.display = solved_facelets(); g.replay_active=false; g.highlight.clear(); g.status="Reset to solved"; }
  ImGui::SameLine(); if (ImGui::Button("Clear (unset)")) { g.display.fill('?'); g.replay_active=false; g.highlight.clear(); g.status="Cleared — paint or Scramble"; }
  ImGui::SameLine(); if (ImGui::Button("Scramble (20)")) { g.display = scramble_facelets(20); g.replay_active=false; g.highlight.clear(); g.status="Scrambled (random HTM 20)"; }
  if (ImGui::Button("Validate")) {
    int u = count_unset(g.display);
    if (u > 0) { g.status = "Error: "+std::to_string(u)+" unset stickers — paint or Scramble first"; g.highlight.clear(); }
    else { auto v = validate_facelets(g.display);
      if (v.error!=FaceletError::Ok) { g.status = std::string("Invalid: ")+to_string(v.error)+" — "+v.message; g.highlight=v.offending; }
      else { g.status="Valid"; g.highlight.clear(); } }
  }
  ImGui::SameLine();
  bool busy = g.solving.load();
  if (!busy && ImGui::Button("Solve")) on_solve_click(g);
  ImGui::SameLine();
  if (busy && ImGui::Button("Cancel")) { g.stop_src.request_stop(); g.status="Cancelling..."; }
  ImGui::SameLine(); ImGui::TextUnformatted(g.status.c_str());
}
static void draw_replay(GuiState& g) {
  if (!g.replay_active) return;
  ImGui::Separator();
  ImGui::Text("Replay: %zu/%zu", g.animated.currentIndex(), g.animated.moveCount());
  if (ImGui::Button("Play")) g.animated.play();
  ImGui::SameLine(); if (ImGui::Button("Pause")) g.animated.pause();
  ImGui::SameLine(); if (ImGui::Button("Step")) g.animated.step();
  ImGui::SameLine(); if (ImGui::Button("Prev")) g.animated.stepBack();
  float spd = g.animated.speed();
  if (ImGui::SliderFloat("Speed", &spd, 0.2f, 4.0f)) g.animated.setSpeed(spd);
  if (g.animated.currentIndex()==g.animated.moveCount() && g.animated.moveCount()>0) {
    if (to_string54(g.animated.facelets()) != to_string54(cube_to_facelets(CubeState::solved())))
      ImGui::TextColored(ImVec4{1,0.3f,0.3f,1}, "ERROR: displayed facelets != solved at end");
    else ImGui::TextColored(ImVec4{0.3f,1,0.3f,1}, "Reached solved state");
  }
}
// ---------- main ----------
// Args are resolved BEFORE any Renderer/GLFW/GL init so that --help and
// --version exit 0 on headless/no-display machines.
int main(int argc, char** argv) {
  const char* const* const_argv = reinterpret_cast<const char* const*>(argv);
  switch (parseCliArgs(argc, const_argv)) {
    case CliAction::Help:
      std::fputs(cliUsage(argv[0]).c_str(), stdout);
      return 0;
    case CliAction::Version:
      std::fputs(cliVersion(), stdout);
      return 0;
    case CliAction::Run:
      break;
  }
  Renderer renderer;
  if (!renderer.init(1280, 800, "Crouton Alpha — Rubik 3D")) {
    std::fprintf(stderr, "Failed to init renderer (no display?)\n");
    return 1;
  }
  GLFWwindow* window = renderer.window();
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");
  GuiState gui; gui.display = solved_facelets();
  auto t_last = std::chrono::steady_clock::now();
  double click_x = 0, click_y = 0; bool click_pending = false;
  char names[6] = {'U','R','F','D','L','B'};
  while (!renderer.shouldClose()) {
    renderer.pollEvents();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - t_last).count(); t_last = now;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { click_x = ImGui::GetIO().MousePos.x; click_y = ImGui::GetIO().MousePos.y; click_pending = true; }
    bool lrel = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    // Paint on click-release with <kDragThresholdPx move & not on UI control.
    // Renderer suppresses camera rotation below the same threshold, so a
    // click paints without rotating at all.
    if (lrel && click_pending) {
      float moved = std::hypot((double)ImGui::GetIO().MousePos.x - click_x,
                                (double)ImGui::GetIO().MousePos.y - click_y);
      if (moved < Renderer::kDragThresholdPx && !ImGui::IsAnyItemHovered()) {
        // HiDPI: ImGui mouse is in window (logical) coords; Renderer needs
        // framebuffer coords. pickWindow scales by fb/window ratio.
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(window, &ww, &wh);
        glfwGetFramebufferSize(window, &fw, &fh);
        int id = renderer.pickWindow((int)click_x, (int)click_y, ww, wh, fw, fh);
        if (0 <= id && id < 54) {
          if (gui.selected_color < 6) gui.display[(std::size_t)id] = names[gui.selected_color];
          else gui.display[(std::size_t)id] = '?';
          gui.highlight.clear();
        }
      }
      click_pending = false;
    }
    ImGui::Begin("Rubik Solver 3D");
    draw_palette(gui); draw_controls(gui); draw_replay(gui); draw_net(gui);
    ImGui::End();
    poll_solve_result(gui);
    renderer.setHighlighted(gui.highlight);
    if (gui.replay_active) { gui.animated.update(dt); renderer.renderAnimated(gui.animated); }
    else {
      renderer.render(gui.display);
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }
  if (gui.worker.joinable()) { gui.stop_src.request_stop(); gui.worker.join(); }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  renderer.shutdown();
  return 0;
}
