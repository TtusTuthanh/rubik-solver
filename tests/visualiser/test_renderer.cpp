#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <GLFW/glfw3.h>
#include "visualiser/renderer.hpp"
#include "core/cube.hpp"
#include "core/moves.hpp"
using namespace rubik::visualiser;
using namespace rubik;
static FaceletArray solvedFacelets(){
  std::string s=to_facelets(CubeState::solved()); FaceletArray a{}; std::copy(s.begin(),s.end(),a.begin()); return a;
}
static bool nearVec(Vec3 a,Vec3 b,float e=1e-4f){ return std::abs(a.x-b.x)<e&&std::abs(a.y-b.y)<e&&std::abs(a.z-b.z)<e; }
TEST(RendererMath, StickerQuadCorners){
  // URF corner U sticker = index 8, cubie (1,1,1); F sticker of same cubie = 2*9+2=20
  EXPECT_TRUE(nearVec(sticker_quad(8).cubiePos,{1,1,1}));
  EXPECT_TRUE(nearVec(sticker_quad(20).cubiePos,{1,1,1}));
  EXPECT_TRUE(nearVec(sticker_quad(8).normal,{0,1,0}));
  EXPECT_TRUE(nearVec(sticker_quad(0).cubiePos,{-1,1,-1})); // ULB on U pos0
  for(int i=0;i<54;++i){ auto q=sticker_quad(i); EXPECT_EQ(q.index,i); EXPECT_EQ(q.face,i/9); }
}
TEST(RendererMath, TurnAxesAndCompletion){
  TurnInfo t; ASSERT_TRUE(turnForToken("R",t)); EXPECT_EQ(t.layerAxis,0); EXPECT_EQ(t.layerValue,1);
  // R: URF(1,1,1) -> UBR(1,1,-1) at progress 1
  EXPECT_TRUE(nearVec(applyTurnToPoint({1,1,1},t,1.0f),{1,1,-1},1e-4f));
  TurnInfo u; ASSERT_TRUE(turnForToken("U",u)); EXPECT_EQ(u.layerAxis,1);
  EXPECT_TRUE(nearVec(applyTurnToPoint({1,1,1},u,1.0f),{-1,1,1},1e-4f)); // URF->UFL? U clockwise from above: front->left? check: rotates -90 about Y: (1,0,1)->(-1,0,1)? x'=z=1? hmm compute below in test tolerance via inverse
  // inverse undoes
  TurnInfo ri; ASSERT_TRUE(turnForToken("R'",ri));
  Vec3 p=applyTurnToPoint({1,1,1},t,1.0f); EXPECT_TRUE(nearVec(applyTurnToPoint(p,ri,1.0f),{1,1,1},1e-4f));
  // non-layer unchanged
  EXPECT_TRUE(nearVec(applyTurnToPoint({-1,1,1},t,1.0f),{-1,1,1}));
  TurnInfo f2; ASSERT_TRUE(turnForToken("F2",f2)); EXPECT_TRUE(nearVec(applyTurnToPoint({1,1,1},f2,1.0f),{-1,-1,1},1e-4f));
  EXPECT_FALSE(turnForToken("X",t)); EXPECT_FALSE(turnForToken("M",t)); EXPECT_FALSE(turnForToken("",t));
}
TEST(Picking, FaceCentresAllFaces){
  const char* names[6]={"U","R","F","D","L","B"};
  for(int face=0;face<6;++face){
    OrbitCamera cam; Vec3 n=faceNormal(face);
    cam.yaw=std::atan2(n.x,n.z); cam.pitch=std::asin(std::clamp(n.y,-1.0f,1.0f));
    if(std::abs(n.y)>0.9f) cam.pitch=std::copysign(1.2f,n.y); // avoid gimbal at pole, still sees that face
    cam.update(0);
    int expect=face*9+4;
    // (centre-pixel ray is offset for U/D at non-pole pitch; projected-centre check below is exact)
    // ray through projected centre sticker also hits it
    int px=0,py=0; ASSERT_TRUE(cam.project(sticker_quad(expect).center,800,600,px,py));
    EXPECT_EQ(cam.pick(px,py,800,600),expect) << "face "<<names[face];
  }
}
TEST(Picking, MissAndOrbit){
  OrbitCamera cam;
  EXPECT_EQ(cam.pick(0,0,800,600),-1);
  EXPECT_EQ(pickRay(Ray{{0,0,10},{0,0,-1}}), 2*9+4); // from +Z centre -> F centre
  EXPECT_EQ(pickRay(Ray{{0,0,10},{0,1,0}}),-1);
  int before=cam.pick(400,300,800,600); EXPECT_NE(before,-1);
  cam.rotate(400,0); cam.update(0);
  int after=cam.pick(400,300,800,600); EXPECT_NE(after,before);
}
TEST(Picking, ProjectInvertsRayForVisibleStickers){
  OrbitCamera cam; // default sees U/R/F
  for(int idx:{8,4,20,44,54-10,0,2,6}){
    auto q=sticker_quad(idx); int px=0,py=0;
    if(!cam.project(q.center,800,600,px,py)) continue;
    // Only assert when sticker is on a front-facing face (normal dot view dir <0)
    if(dot(q.normal, cam.forward())>=-0.05f) continue;
    EXPECT_EQ(cam.pick(px,py,800,600),idx);
  }
}
TEST(AnimatedCube, StepToSolvedAndPrev){
  CubeState scr=apply(CubeState::solved(), parse_moves("R U").value());
  std::string sf=to_facelets(scr); FaceletArray a{}; std::copy(sf.begin(),sf.end(),a.begin());
  AnimatedCube ac; ac.setState(a); ac.setMoves({"U'","R'"}); // inverse of "R U" is "U' R'"
  EXPECT_FALSE(ac.animating()); EXPECT_EQ(ac.currentIndex(),0u);
  ac.step(); EXPECT_TRUE(ac.animating()); EXPECT_EQ(ac.currentToken().value(),"U'");
  for(int i=0;i<100&&ac.animating();++i) ac.update(0.05f);
  EXPECT_EQ(ac.currentIndex(),1u);
  ac.step(); for(int i=0;i<100&&ac.animating();++i) ac.update(0.05f);
  EXPECT_EQ(ac.currentIndex(),2u);
  std::string fin(ac.facelets().begin(),ac.facelets().end());
  EXPECT_EQ(fin,to_facelets(CubeState::solved()));
  ac.stepBack(); EXPECT_NE(std::string(ac.facelets().begin(),ac.facelets().end()),fin);
  EXPECT_EQ(ac.currentIndex(),1u);
}
TEST(AnimatedCube, PlayPauseSpeedAndInstant){
  AnimatedCube ac; ac.setState(solvedFacelets()); ac.setMoves({"U","U","U","U"});
  ac.setSpeed(10.0f); ac.play(); EXPECT_TRUE(ac.playing());
  int steps=0; while(ac.currentIndex()<4u && steps++<1000) ac.update(0.01f);
  EXPECT_EQ(ac.currentIndex(),4u); EXPECT_FALSE(ac.animating());
  CubeState s=applyTokenInstant(CubeState::solved(),"R"); EXPECT_FALSE(is_solved(s));
  EXPECT_TRUE(is_solved(applyTokenInstant(s,"R'")));
  // progress in [0,1]
  AnimatedCube ac2; ac2.setState(solvedFacelets()); ac2.setMoves({"F"}); ac2.step(); ac2.update(0.01f);
  EXPECT_GE(ac2.currentProgress(),0.0f); EXPECT_LE(ac2.currentProgress(),1.0f);
}
TEST(RendererMath, CameraZoomRotateClamp){
  OrbitCamera cam; float d0=cam.distance; cam.zoom(0.5f); EXPECT_LT(cam.distance,d0);
  cam.rotate(1e6f,1e6f); cam.update(0); EXPECT_LE(cam.pitch,1.53f); EXPECT_GE(cam.pitch,-1.53f);
}
TEST(Highlight, SetClearHeadless){
  // No window/GL needed: highlight state lives on Renderer.
  Renderer r;
  EXPECT_TRUE(r.highlighted().empty());
  EXPECT_FALSE(r.isHighlighted(0));
  r.setHighlighted({8, 9, 20});
  EXPECT_EQ(r.highlighted().size(), 3u);
  EXPECT_TRUE(r.isHighlighted(8));
  EXPECT_TRUE(r.isHighlighted(9));
  EXPECT_TRUE(r.isHighlighted(20));
  EXPECT_FALSE(r.isHighlighted(0));
  // Out-of-range indices are filtered, duplicates kept as given.
  r.setHighlighted({0, 54, -1, 53});
  EXPECT_EQ(r.highlighted().size(), 2u);
  EXPECT_TRUE(r.isHighlighted(0));
  EXPECT_TRUE(r.isHighlighted(53));
  r.clearHighlight();
  EXPECT_TRUE(r.highlighted().empty());
  EXPECT_FALSE(r.isHighlighted(0));
  // Re-set then clear again (validation flow: error -> highlight -> paint clears).
  r.setHighlighted({1, 2, 3});
  EXPECT_EQ(r.highlighted().size(), 3u);
  r.setHighlighted({});
  EXPECT_TRUE(r.highlighted().empty());
}
TEST(Input, ClickDoesNotRotateDragDoes){
  // Renderer handles motion headlessly (no GL): press + small motion
  // must leave yaw/pitch untouched; crossing the threshold starts orbit.
  Renderer r;
  float yaw0 = r.camera().yaw;
  float pitch0 = r.camera().pitch;
  r.handleMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 400.0, 300.0);
  EXPECT_FALSE(r.dragActive());
  r.handleMouseMotion(402.0, 301.0);
  EXPECT_FALSE(r.dragActive());
  EXPECT_FLOAT_EQ(r.camera().yaw, yaw0);
  EXPECT_FLOAT_EQ(r.camera().pitch, pitch0);
  r.handleMouseMotion(404.0, 302.0);
  EXPECT_FALSE(r.dragActive());
  EXPECT_FLOAT_EQ(r.camera().yaw, yaw0);
  EXPECT_FLOAT_EQ(r.camera().pitch, pitch0);
  // Exceeding the 6px threshold activates the drag and rotates.
  r.handleMouseMotion(412.0, 300.0);
  EXPECT_TRUE(r.dragActive());
  EXPECT_NE(r.camera().yaw, yaw0);
  // Further motion keeps rotating; release is not a click-drag.
  float yaw1 = r.camera().yaw;
  r.handleMouseMotion(420.0, 300.0);
  EXPECT_NE(r.camera().yaw, yaw1);
  r.handleMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 420.0, 300.0);
  EXPECT_FALSE(r.dragActive());
  // Next click alone does not rotate again.
  float yaw2 = r.camera().yaw;
  r.handleMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 100.0, 100.0);
  r.handleMouseMotion(101.0, 101.0);
  EXPECT_FLOAT_EQ(r.camera().yaw, yaw2);
  r.handleMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 101.0, 101.0);
}
TEST(Picking, HiDpiPickScalesToFramebuffer){
  // windowToFramebufferCoord is pure math; pickWindow must agree with
  // pick at the scaled framebuffer pixel (HiDPI 2x case).
  EXPECT_EQ(windowToFramebufferCoord(400, 800, 1600), 800);
  EXPECT_EQ(windowToFramebufferCoord(300, 600, 1200), 600);
  EXPECT_EQ(windowToFramebufferCoord(0, 800, 1600), 0);
  EXPECT_EQ(windowToFramebufferCoord(400, 0, 1600), 400); // 1:1 fallback
  OrbitCamera cam;
  int px=0, py=0;
  ASSERT_TRUE(cam.project(sticker_quad(4).center, 1600, 1200, px, py));
  int windowX = (int)std::lround(double(px)/2.0);
  int windowY = (int)std::lround(double(py)/2.0);
  int fx = windowToFramebufferCoord(windowX, 800, 1600);
  int fy = windowToFramebufferCoord(windowY, 600, 1200);
  EXPECT_EQ(cam.pickWindow(windowX, windowY, 800, 600, 1600, 1200),
            cam.pick(fx, fy, 1600, 1200));
  // Renderer level (headless: no window, explicit sizes).
  Renderer r;
  EXPECT_EQ(r.pickWindow(windowX, windowY, 800, 600, 1600, 1200),
            r.camera().pick(fx, fy, 1600, 1200));
  // Degenerate sizes are -1, matching pick.
  EXPECT_EQ(cam.pickWindow(0, 0, 800, 600, 0, 0), -1);
}
TEST(SpecOrder, URFDLBStickerQuadMatchesCubeTables){
  // Independent proof that URFDLB order + per-face row-major layout agree
  // across sources (no re-derivation from scratch):
  //   docs/SPEC.md:51 (face order URFDLB, fixed in P1)
  //   src/core/cube.hpp:4-10 (URFDLB, row-major 1-9, CP/EP cubie order)
  //   src/core/cube.cpp:12-72 (kCornerFacelets/kEdgeFacelets tables),
  //       to_facelets at :227
  //   src/visualiser/facelet.cpp:19-54 (mirrored tables),
  //       facelet.hpp:7-20 (sourced convention)
  //   src/visualiser/renderer.cpp sticker_quad (face=idx/9, row/col row-major)
  //   tests/core/test_cube.cpp:29-62 (SolvedFacelets + roundtrips)
  // The numeric tables below are the expanded values of cube.cpp:26-72
  // and facelet.cpp:29-54 (identical numbers; facelet.cpp uses 0*9/1*9
  // arithmetic, cube.cpp uses kFaceU..kFaceB). Cubie positions follow
  // cube.hpp:9-10 order URF,UFL,ULB,UBR,DFR,DLF,DBL,DRB /
  // UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR.
  EXPECT_EQ(to_facelets(CubeState::solved()),
            "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB");
  auto parsed = parse_facelets(to_facelets(CubeState::solved()));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed.value(), CubeState::solved());
  // Corner position -> 3 facelet indices (cube.cpp:26-43).
  const int kCornerFacelets[8][3] = {
      {8, 9, 20},   {6, 18, 38},  {0, 36, 47},  {2, 45, 11},
      {29, 26, 15}, {27, 44, 24}, {33, 53, 42}, {35, 17, 51}};
  const Vec3 kCornerPos[8] = {{1,1,1}, {-1,1,1}, {-1,1,-1}, {1,1,-1},
                              {1,-1,1}, {-1,-1,1}, {-1,-1,-1}, {1,-1,-1}};
  for(int p=0;p<8;++p){
    for(int k=0;k<3;++k){
      int idx = kCornerFacelets[p][k];
      EXPECT_EQ(sticker_quad(idx).face, idx/9);
      EXPECT_TRUE(nearVec(sticker_quad(idx).cubiePos, kCornerPos[p]))
          << "corner " << p << " sticker " << idx;
    }
  }
  // Edge position -> 2 facelet indices (cube.cpp:47-72).
  const int kEdgeFacelets[12][2] = {{5,10}, {7,19}, {3,37}, {1,46},
      {32,16}, {28,25}, {30,43}, {34,52}, {23,12}, {21,41},
      {50,39}, {48,14}};
  const Vec3 kEdgePos[12] = {{1,1,0}, {0,1,1}, {-1,1,0}, {0,1,-1},
      {1,-1,0}, {0,-1,1}, {-1,-1,0}, {0,-1,-1},
      {1,0,1}, {-1,0,1}, {-1,0,-1}, {1,0,-1}};
  for(int p=0;p<12;++p){
    for(int k=0;k<2;++k){
      int idx = kEdgeFacelets[p][k];
      EXPECT_TRUE(nearVec(sticker_quad(idx).cubiePos, kEdgePos[p]))
          << "edge " << p << " sticker " << idx;
    }
  }
  // Face order U R F D L B: sticker 4 of each face is its centre.
  for(int f=0;f<6;++f){
    EXPECT_EQ(sticker_quad(f*9+4).face, f);
    EXPECT_EQ(sticker_quad(f*9+4).row, 1);
    EXPECT_EQ(sticker_quad(f*9+4).col, 1);
  }
}
TEST(CliArgs, HelpVersionExitBeforeGl){
  // Headless regression: --help/--version must be handled before any
  // GLFW/GL init (main_3d.cpp main() must not touch the Renderer).
  // parseCliArgs is pure (no GL), so this test runs headless.
  const char* help[] = {"rubik-visualiser3d", "--help"};
  EXPECT_EQ(parseCliArgs(2, help), CliAction::Help);
  const char* h[] = {"rubik-visualiser3d", "-h"};
  EXPECT_EQ(parseCliArgs(2, h), CliAction::Help);
  const char* ver[] = {"rubik-visualiser3d", "--version"};
  EXPECT_EQ(parseCliArgs(2, ver), CliAction::Version);
  const char* v[] = {"rubik-visualiser3d", "-v"};
  EXPECT_EQ(parseCliArgs(2, v), CliAction::Version);
  const char* none[] = {"rubik-visualiser3d"};
  EXPECT_EQ(parseCliArgs(1, none), CliAction::Run);
  const char* other[] = {"rubik-visualiser3d", "--bogus"};
  EXPECT_EQ(parseCliArgs(2, other), CliAction::Run);
  EXPECT_EQ(parseCliArgs(0, nullptr), CliAction::Run);
  // Usage text names the program and mentions the flags.
  std::string usage = cliUsage("rubik-visualiser3d");
  EXPECT_NE(usage.find("rubik-visualiser3d"), std::string::npos);
  EXPECT_NE(usage.find("--help"), std::string::npos);
  EXPECT_NE(usage.find("--version"), std::string::npos);
  EXPECT_FALSE(std::string(cliVersion()).empty());
}
