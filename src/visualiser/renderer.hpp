#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>
#include "core/cube.hpp"
struct GLFWwindow;
#include "visualiser/facelet.hpp"
// FaceletArray lives in visualiser/facelet.hpp (single definition); not redefined here.
namespace rubik::visualiser {
inline constexpr float kPi = 3.14159265358979323846f;
struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
inline float length(const Vec3& v) { return std::sqrt(dot(v,v)); }
inline Vec3 normalize(const Vec3& v) { float l=length(v); return l>1e-12f? v/l : Vec3{0,0,0}; }
struct Mat4 { float m[16]{}; static Mat4 identity(){ Mat4 r; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f; return r;} static Mat4 zero(){ return Mat4{}; } };
inline Mat4 operator*(const Mat4& a, const Mat4& b){ Mat4 r=Mat4::zero(); for(int c=0;c<4;++c) for(int row=0;row<4;++row){ float s=0; for(int k=0;k<4;++k) s+=a.m[k*4+row]*b.m[c*4+k]; r.m[c*4+row]=s;} return r; }
inline Vec3 transformPoint(const Mat4& m, const Vec3& p){ float x=m.m[0]*p.x+m.m[4]*p.y+m.m[8]*p.z+m.m[12]; float y=m.m[1]*p.x+m.m[5]*p.y+m.m[9]*p.z+m.m[13]; float z=m.m[2]*p.x+m.m[6]*p.y+m.m[10]*p.z+m.m[14]; float w=m.m[3]*p.x+m.m[7]*p.y+m.m[11]*p.z+m.m[15]; return std::abs(w)>1e-12f? Vec3{x/w,y/w,z/w}:Vec3{x,y,z}; }
inline Mat4 matPerspective(float fovY,float aspect,float np,float fp){ Mat4 r=Mat4::zero(); float t=std::tan(fovY*0.5f); r.m[0]=1.0f/(aspect*t); r.m[5]=1.0f/t; r.m[10]=(fp+np)/(np-fp); r.m[11]=-1.0f; r.m[14]=(2.0f*fp*np)/(np-fp); return r; }
inline Mat4 matLookAt(const Vec3& eye,const Vec3& center,const Vec3& up){ Vec3 f=normalize(center-eye); Vec3 s=normalize(cross(f,up)); Vec3 u=cross(s,f); Mat4 r=Mat4::identity(); r.m[0]=s.x;r.m[4]=s.y;r.m[8]=s.z; r.m[1]=u.x;r.m[5]=u.y;r.m[9]=u.z; r.m[2]=-f.x;r.m[6]=-f.y;r.m[10]=-f.z; r.m[12]=-dot(s,eye); r.m[13]=-dot(u,eye); r.m[14]=dot(f,eye); return r; }
inline Mat4 matRotationAxis(const Vec3& axisIn,float ang){ Vec3 a=normalize(axisIn); float c=std::cos(ang),s=std::sin(ang),t=1.0f-c; Mat4 r=Mat4::identity(); float rm[3][3]={{t*a.x*a.x+c,t*a.x*a.y-s*a.z,t*a.x*a.z+s*a.y},{t*a.x*a.y+s*a.z,t*a.y*a.y+c,t*a.y*a.z-s*a.x},{t*a.x*a.z-s*a.y,t*a.y*a.z+s*a.x,t*a.z*a.z+c}}; for(int row=0;row<3;++row) for(int col=0;col<3;++col) r.m[col*4+row]=rm[row][col]; return r; }
inline Mat4 matTranslation(const Vec3& p){ Mat4 r=Mat4::identity(); r.m[12]=p.x;r.m[13]=p.y;r.m[14]=p.z; return r; }
inline Vec3 rotateVec(const Vec3& v,const Vec3& axis,float ang){ return transformPoint(matRotationAxis(axis,ang),v); }
Vec3 faceNormal(int face); Vec3 faceU(int face); Vec3 faceV(int face);
struct StickerQuad{ int index=0; int face=0,row=0,col=0; Vec3 center; Vec3 normal; Vec3 uDir,vDir; float half=0.46f; Vec3 cubiePos; std::array<Vec3,4> corners() const; };
StickerQuad sticker_quad(int index); Vec3 cubiePositionForSticker(int index);
struct TurnInfo{ char faceLetter='U'; Vec3 axis{0,1,0}; int layerAxis=1; int layerValue=1; float angleRad=0; int quarters=1; };
bool turnForToken(const std::string& token, TurnInfo& out);
bool isInTurningLayer(const Vec3& cubiePos,const TurnInfo& t);
Vec3 applyTurnToPoint(const Vec3& p,const TurnInfo& t,float progress01);
Mat4 cubieTransform(const Vec3& cubiePos,const TurnInfo& t,float progress01);
CubeState applyTokenInstant(const CubeState& state,const std::string& token);
struct Ray{ Vec3 origin,direction; };
// Scale a window/logical point coordinate to framebuffer pixels.
// Zero windowSize or fbSize falls back to identity (1:1, e.g. headless).
inline int windowToFramebufferCoord(int windowCoord,int windowSize,int fbSize){
  if(windowSize<=0 || fbSize<=0) return windowCoord;
  return (int)std::lround(double(windowCoord)*double(fbSize)/double(windowSize));
}
// CLI gate — pure (no GLFW/GL), so main can bail out on --help/--version
// before any window/context/Renderer::init() exists. Headless-safe.
enum class CliAction { Run, Help, Version };
inline CliAction parseCliArgs(int argc, const char* const* argv) {
  for (int i = 1; i < argc; ++i) {
    if (!argv[i]) continue;
    std::string a = argv[i];
    if (a == "--help" || a == "-h") return CliAction::Help;
    if (a == "--version" || a == "-v") return CliAction::Version;
  }
  return CliAction::Run;
}
inline std::string cliUsage(const char* prog) {
  return std::string("Usage: ") + (prog ? prog : "rubik-visualiser3d") +
      " [--help] [--version]\n\nInteractive 3D Rubik's cube viewer (ImGui/GLFW/OpenGL 3.3 core).\n\nOptions:\n  --help, -h      Print this usage and exit 0 (no GUI/GL needed)\n  --version, -v   Print version and exit 0 (no GUI/GL needed)\n\nWith no options, opens the 3D GUI window (requires a display).\n";
}
inline const char* cliVersion() { return "rubik-visualiser3d 0.1.0\n"; }
struct OrbitCamera{
 float yaw=0.61f; float pitch=0.44f; float distance=6.0f; Vec3 target{0,0,0}; float fovY=0.7853982f;
 Vec3 eye() const; Vec3 forward() const; Vec3 right() const; Vec3 up() const;
 Mat4 viewMatrix() const; Mat4 projectionMatrix(float aspect) const;
 void rotate(float dxPixels,float dyPixels); void zoom(float factor); void update(float dt);
 Ray rayThroughPixel(int mouseX,int mouseY,int viewportW,int viewportH) const;
 // HiDPI variant: mouse in window/logical coords; viewport fb size is the
 // framebuffer size. See windowToFramebufferCoord.
 Ray rayThroughPixelWindow(int windowX,int windowY,int windowW,int windowH,int fbW,int fbH) const;
 bool project(const Vec3& world,int vw,int vh,int& outX,int& outY) const;
 int pick(int mouseX,int mouseY,int viewportW,int viewportH) const;
 int pickWindow(int windowX,int windowY,int windowW,int windowH,int fbW,int fbH) const;
};
int pickRay(const Ray& ray);
class AnimatedCube{
 public:
 void setState(const FaceletArray& facelets); void setMoves(std::vector<std::string> tokens);
 bool update(float dt); float currentProgress() const { return progress_; } bool animating() const { return animating_; } bool playing() const { return playing_; }
 void play(); void pause(); void togglePlay();
 void setSpeed(float t){ speed_=t>0?t:0.1f; } float speed() const { return speed_; }
 void step(); void next(){ step(); } void stepBack(); void prev(){ stepBack(); } void reset();
 std::size_t currentIndex() const { return index_; } std::size_t moveCount() const { return moves_.size(); }
 std::optional<std::string> currentToken() const; std::optional<TurnInfo> currentTurn() const;
 const FaceletArray& facelets() const { return colors_; }
 float currentAngleRad() const; bool isTurningLayer(const Vec3& cubiePos) const;
 private: void completeCurrent(); FaceletArray colors_{}; std::vector<std::string> moves_; std::size_t index_=0; float progress_=0; bool animating_=false; bool playing_=false; float speed_=1.5f;
};
class Renderer{
 public:
 Renderer()=default; ~Renderer(); Renderer(const Renderer&)=delete; Renderer& operator=(const Renderer&)=delete;
 bool init(int width,int height,const char* title); void shutdown(); bool shouldClose() const; void render(const FaceletArray& colors); void renderAnimated(const AnimatedCube& animated); void pollEvents();
 OrbitCamera& camera(){ return camera_; } const OrbitCamera& camera() const { return camera_; }
 GLFWwindow* window() const { return window_; }
 // Highlight state (replaces the old grey-flash overlay_copy hack in
 // main_3d.cpp): drawQuads tints highlighted stickers. Headless-safe.
 void setHighlighted(std::vector<int> indices);
 void clearHighlight() { highlight_.clear(); }
 const std::vector<int>& highlighted() const { return highlight_; }
 bool isHighlighted(int idx) const;
 // Drag threshold: press starts a candidate click; camera rotates only
 // after the pointer moves >= kDragThresholdPx from the press point, so
 // a click paints without rotating at all (see handleMouseMotion).
 static constexpr double kDragThresholdPx = 6.0;
 bool dragActive() const { return dragActive_; }
 // Framebuffer-space pick: x/y must already be framebuffer pixels with
 // (w,h) the framebuffer size. For HiDPI window (logical) coords use
 // pickWindow() instead, which scales to framebuffer space first.
 int pick(int x,int y,int w,int h) const { return camera_.pick(x,y,w,h); }
 // HiDPI pick: (windowX,windowY) in window/logical points, (windowW,
 // windowH) the window size, (fbW,fbH) the framebuffer size. Scales to
 // framebuffer coords before ray-casting.
 int pickWindow(int windowX,int windowY,int windowW,int windowH,int fbW,int fbH) const {
   return camera_.pickWindow(windowX,windowY,windowW,windowH,fbW,fbH);
 }
 void handleMouseButton(int button,int action,double x,double y); void handleMouseMotion(double x,double y); void handleScroll(double xOffset,double yOffset);
 private: bool ensureGL(); void drawQuads(const FaceletArray& colors,const AnimatedCube* animated);
 GLFWwindow* window_=nullptr; OrbitCamera camera_; unsigned int vao_=0,vbo_=0,program_=0; int width_=0,height_=0; bool glReady_=false; bool dragging_=false; double lastX_=0,lastY_=0;
 double pressX_=0,pressY_=0; bool dragActive_=false; std::vector<int> highlight_;
};
} // namespace rubik::visualiser
