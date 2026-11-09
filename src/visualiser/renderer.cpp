#include "visualiser/renderer.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include "core/moves.hpp"
#ifdef _WIN32
// Windows opengl32.dll does not export core-profile (3.3) symbols; load them
// via wglGetProcAddress after context creation (no glad dependency).
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glcorearb.h>
static PFNGLCREATESHADERPROC glCreateShaderFn; static PFNGLSHADERSOURCEPROC glShaderSourceFn;
static PFNGLCOMPILESHADERPROC glCompileShaderFn; static PFNGLGETSHADERIVPROC glGetShaderivFn;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLogFn; static PFNGLCREATEPROGRAMPROC glCreateProgramFn;
static PFNGLATTACHSHADERPROC glAttachShaderFn; static PFNGLLINKPROGRAMPROC glLinkProgramFn;
static PFNGLGETPROGRAMIVPROC glGetProgramivFn; static PFNGLDELETESHADERPROC glDeleteShaderFn;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArraysFn; static PFNGLGENBUFFERSPROC glGenBuffersFn;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArrayFn; static PFNGLBINDBUFFERPROC glBindBufferFn;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArrayFn; static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointerFn;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLogFn; static PFNGLENABLEPROC glEnableFn;
static PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArraysFn; static PFNGLDELETEBUFFERSPROC glDeleteBuffersFn;
static PFNGLDELETEPROGRAMPROC glDeleteProgramFn; static PFNGLBUFFERDATAPROC glBufferDataFn;
static PFNGLUSEPROGRAMPROC glUseProgramFn; static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocationFn;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fvFn; static PFNGLVIEWPORTPROC glViewportFn;
static PFNGLCLEARCOLORPROC glClearColorFn; static PFNGLCLEARPROC glClearFn; static PFNGLDRAWARRAYSPROC glDrawArraysFn;
#define glCreateShader glCreateShaderFn
#define glShaderSource glShaderSourceFn
#define glCompileShader glCompileShaderFn
#define glGetShaderiv glGetShaderivFn
#define glGetShaderInfoLog glGetShaderInfoLogFn
#define glCreateProgram glCreateProgramFn
#define glAttachShader glAttachShaderFn
#define glLinkProgram glLinkProgramFn
#define glGetProgramiv glGetProgramivFn
#define glDeleteShader glDeleteShaderFn
#define glGenVertexArrays glGenVertexArraysFn
#define glGenBuffers glGenBuffersFn
#define glBindVertexArray glBindVertexArrayFn
#define glBindBuffer glBindBufferFn
#define glEnableVertexAttribArray glEnableVertexAttribArrayFn
#define glVertexAttribPointer glVertexAttribPointerFn
#define glGetProgramInfoLog glGetProgramInfoLogFn
#define glEnable glEnableFn
#define glDeleteVertexArrays glDeleteVertexArraysFn
#define glDeleteBuffers glDeleteBuffersFn
#define glDeleteProgram glDeleteProgramFn
#define glBufferData glBufferDataFn
#define glUseProgram glUseProgramFn
#define glGetUniformLocation glGetUniformLocationFn
#define glUniformMatrix4fv glUniformMatrix4fvFn
#define glViewport glViewportFn
#define glClearColor glClearColorFn
#define glClear glClearFn
#define glDrawArrays glDrawArraysFn
static bool loadGL(){
  struct E{ void** dst; const char* name; };
  E table[] = {
    {(void**)&glCreateShaderFn,"glCreateShader"},{(void**)&glShaderSourceFn,"glShaderSource"},
    {(void**)&glCompileShaderFn,"glCompileShader"},{(void**)&glGetShaderivFn,"glGetShaderiv"},
    {(void**)&glGetShaderInfoLogFn,"glGetShaderInfoLog"},{(void**)&glCreateProgramFn,"glCreateProgram"},
    {(void**)&glAttachShaderFn,"glAttachShader"},{(void**)&glLinkProgramFn,"glLinkProgram"},
    {(void**)&glGetProgramivFn,"glGetProgramiv"},{(void**)&glDeleteShaderFn,"glDeleteShader"},
    {(void**)&glGenVertexArraysFn,"glGenVertexArrays"},{(void**)&glGenBuffersFn,"glGenBuffers"},
    {(void**)&glBindVertexArrayFn,"glBindVertexArray"},{(void**)&glBindBufferFn,"glBindBuffer"},
    {(void**)&glEnableVertexAttribArrayFn,"glEnableVertexAttribArray"},{(void**)&glVertexAttribPointerFn,"glVertexAttribPointer"},
    {(void**)&glGetProgramInfoLogFn,"glGetProgramInfoLog"},{(void**)&glEnableFn,"glEnable"},
    {(void**)&glDeleteVertexArraysFn,"glDeleteVertexArrays"},{(void**)&glDeleteBuffersFn,"glDeleteBuffers"},
    {(void**)&glDeleteProgramFn,"glDeleteProgram"},{(void**)&glBufferDataFn,"glBufferData"},
    {(void**)&glUseProgramFn,"glUseProgram"},{(void**)&glGetUniformLocationFn,"glGetUniformLocation"},
    {(void**)&glUniformMatrix4fvFn,"glUniformMatrix4fv"},{(void**)&glViewportFn,"glViewport"},
    {(void**)&glClearColorFn,"glClearColor"},{(void**)&glClearFn,"glClear"},{(void**)&glDrawArraysFn,"glDrawArrays"}};
  // Ensure opengl32.dll is loaded before calling GetProcAddress on it:
  // GetModuleHandleA returns NULL if the module is not yet loaded/linked.
  HMODULE hGLModule = GetModuleHandleA("opengl32.dll");
  if(!hGLModule) hGLModule = LoadLibraryA("opengl32.dll");
  for(auto& e: table){
    *e.dst = (void*)wglGetProcAddress(e.name);
    // Windows does not export OpenGL 1.1 entry points through wglGetProcAddress;
    // resolve them from opengl32.dll instead (uniform fallback for all entries).
    if(!*e.dst && hGLModule) *e.dst = (void*)GetProcAddress(hGLModule, e.name);
    if(!*e.dst){
      DWORD glErr = GetLastError();
      std::cerr<<"renderer: failed to load GL function "<<e.name<<" (GetLastError="<<glErr<<")\n";
      return false;
    }
  }
  return true;
}
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glcorearb.h>
static bool loadGL(){ return true; }
#endif
#include <GLFW/glfw3.h>
namespace rubik::visualiser {
namespace {
constexpr int kClampIdx(float v){ int i = (int)std::lround(v)+1; return std::clamp(i,0,2); }
} // namespace
Vec3 faceNormal(int f){ switch(f){case 0:return{0,1,0};case 1:return{1,0,0};case 2:return{0,0,1};case 3:return{0,-1,0};case 4:return{-1,0,0};default:return{0,0,-1};} }
Vec3 faceU(int f){ switch(f){case 0:return{1,0,0};case 1:return{0,0,-1};case 2:return{1,0,0};case 3:return{1,0,0};case 4:return{0,0,1};default:return{-1,0,0};} }
Vec3 faceV(int f){ switch(f){case 0:return{0,0,1};case 1:return{0,-1,0};case 2:return{0,-1,0};case 3:return{0,0,-1};case 4:return{0,-1,0};default:return{0,-1,0};} }
std::array<Vec3,4> StickerQuad::corners() const { return {center - uDir*half - vDir*half, center + uDir*half - vDir*half, center + uDir*half + vDir*half, center - uDir*half + vDir*half}; }
StickerQuad sticker_quad(int idx){
  StickerQuad q; q.index=idx; q.face=idx/9; int pos=idx%9; q.row=pos/3; q.col=pos%3;
  q.normal=faceNormal(q.face); q.uDir=faceU(q.face); q.vDir=faceV(q.face);
  q.cubiePos = q.normal*1.0f + q.uDir*float(q.col-1) + q.vDir*float(q.row-1);
  q.center = q.normal*1.5f + q.uDir*float(q.col-1) + q.vDir*float(q.row-1);
  return q;
}
Vec3 cubiePositionForSticker(int idx){ return sticker_quad(idx).cubiePos; }
bool turnForToken(const std::string& tok, TurnInfo& out){
  if(tok.empty()||tok.size()>3) return false;
  char f=tok[0]; int quarters=1;
  if(tok.size()==2){ if(tok[1]=='\'') quarters=-1; else if(tok[1]=='2') quarters=2; else return false; }
  else if(tok.size()!=1) return false;
  int axis; int layer; bool positiveFace;
  switch(f){ case 'U': axis=1; layer=1; positiveFace=true; break; case 'D': axis=1; layer=-1; positiveFace=false; break;
    case 'R': axis=0; layer=1; positiveFace=true; break; case 'L': axis=0; layer=-1; positiveFace=false; break;
    case 'F': axis=2; layer=1; positiveFace=true; break; case 'B': axis=2; layer=-1; positiveFace=false; break; default: return false; }
  out.faceLetter=f; out.layerAxis=axis; out.layerValue=layer; out.quarters=quarters;
  out.axis=Vec3{0,0,0}; out.axis[axis]=1.0f;
  // Clockwise seen from outside face = negative rotation about positive axis.
  float base = positiveFace ? -1.0f : 1.0f;
  out.angleRad = base * float(quarters) * (kPi*0.5f);
  return true;
}
bool isInTurningLayer(const Vec3& p,const TurnInfo& t){ return std::lround(p[t.layerAxis])==t.layerValue; }
Vec3 applyTurnToPoint(const Vec3& p,const TurnInfo& t,float prog){ if(!isInTurningLayer(p,t)) return p; return rotateVec(p,t.axis,t.angleRad*std::clamp(prog,0.0f,1.0f)); }
Mat4 cubieTransform(const Vec3& p,const TurnInfo& t,float prog){ if(!isInTurningLayer(p,t)) return Mat4::identity(); return matRotationAxis(t.axis,t.angleRad*std::clamp(prog,0.0f,1.0f)); }
CubeState applyTokenInstant(const CubeState& s,const std::string& tok){ return apply(s, Move{tok}); }
// ---- Camera ----
Vec3 OrbitCamera::eye() const { float cp=std::cos(pitch); return target + Vec3{cp*std::sin(yaw), std::sin(pitch), cp*std::cos(yaw)}*distance; }
Vec3 OrbitCamera::forward() const { return normalize(target-eye()); }
Vec3 OrbitCamera::right() const { return normalize(cross(forward(), Vec3{0,1,0})); }
Vec3 OrbitCamera::up() const { return cross(right(), forward()); }
Mat4 OrbitCamera::viewMatrix() const { Vec3 upv = std::abs(forward().y)>0.999f? Vec3{0,0,-1}:Vec3{0,1,0}; return matLookAt(eye(),target,upv); }
Mat4 OrbitCamera::projectionMatrix(float a) const { return matPerspective(fovY,a,0.1f,100.0f); }
void OrbitCamera::rotate(float dx,float dy){ yaw -= dx*0.005f; pitch += dy*0.005f; pitch=std::clamp(pitch,-1.53f,1.53f); }
void OrbitCamera::zoom(float f){ distance=std::clamp(distance*f,3.0f,20.0f); }
void OrbitCamera::update(float){ pitch=std::clamp(pitch,-1.53f,1.53f); distance=std::clamp(distance,3.0f,20.0f); }
Ray OrbitCamera::rayThroughPixel(int mx,int my,int vw,int vh) const {
  float aspect = vh>0? float(vw)/float(vh):1.0f; float t=std::tan(fovY*0.5f);
  float ndcX = (2.0f*float(mx)/float(vw)-1.0f)*aspect*t;
  float ndcY = (1.0f-2.0f*float(my)/float(vh))*t;
  Vec3 dir = normalize(forward() + right()*ndcX + up()*ndcY);
  return {eye(), dir};
}
Ray OrbitCamera::rayThroughPixelWindow(int windowX,int windowY,int windowW,int windowH,int fbW,int fbH) const {
  int fx = windowToFramebufferCoord(windowX, windowW, fbW);
  int fy = windowToFramebufferCoord(windowY, windowH, fbH);
  return rayThroughPixel(fx, fy, fbW, fbH);
}
bool OrbitCamera::project(const Vec3& w,int vw,int vh,int& ox,int& oy) const {
  Vec3 v=transformPoint(viewMatrix(),w); if(v.z>=-0.1f) return false;
  float aspect=float(vw)/float(vh); float t=std::tan(fovY*0.5f);
  float ndcX=v.x/(-v.z*t*aspect), ndcY=v.y/(-v.z*t);
  ox=(int)std::lround((ndcX+1.0f)*0.5f*float(vw)); oy=(int)std::lround((1.0f-ndcY)*0.5f*float(vh)); return true;
}
int pickRay(const Ray& ray){
  const float h=1.5f;
  float tmin=0.0f;
  float tmax=1e30f;
  int hitAxis=-1;
  for(int a=0;a<3;++a){
    float o=ray.origin[a];
    float d=ray.direction[a];
    if(std::abs(d)<1e-9f){
      if(o<-h||o>h) return -1;
    } else {
      float t1=(-h-o)/d;
      float t2=(h-o)/d;
      float lo=std::min(t1,t2);
      float hi=std::max(t1,t2);
      if(lo>tmin){
        tmin=lo;
        hitAxis=a;
      }
      tmax=std::min(tmax,hi);
      if(tmin>tmax) return -1;
    }
  }
  if(tmax<0) return -1;
  if(hitAxis<0) return -1;
  Vec3 hit=ray.origin+ray.direction*tmin;
  // face from entry axis + sign of hit coordinate
  int face=-1;
  if(hitAxis==1) face = hit.y>0?0:3; else if(hitAxis==0) face = hit.x>0?1:4; else face = hit.z>0?2:5;
  Vec3 u=faceU(face), v=faceV(face);
  int col=kClampIdx(dot(hit,u)), row=kClampIdx(dot(hit,v));
  return face*9+row*3+col;
}
int OrbitCamera::pick(int x,int y,int w,int h) const {
  if(w<=0||h<=0) return -1;
  return pickRay(rayThroughPixel(x,y,w,h));
}
int OrbitCamera::pickWindow(int windowX,int windowY,int windowW,int windowH,int fbW,int fbH) const {
  if(fbW<=0||fbH<=0) return -1;
  return pickRay(rayThroughPixelWindow(windowX,windowY,windowW,windowH,fbW,fbH));
}
// ---- AnimatedCube ----
void AnimatedCube::setState(const FaceletArray& f){ colors_=f; }
void AnimatedCube::setMoves(std::vector<std::string> t){ moves_=std::move(t); index_=0; progress_=0; animating_=false; playing_=false; }
void AnimatedCube::completeCurrent(){
  if(index_>=moves_.size()) return;
  std::string tok=moves_[index_];
  auto parsed=parse_facelets(std::string(colors_.begin(),colors_.end()));
  if(parsed){ CubeState ns=apply(parsed.value(), Move{tok}); std::string s=to_facelets(ns); std::copy(s.begin(),s.end(),colors_.begin()); }
  ++index_; progress_=0; animating_=false;
}
bool AnimatedCube::update(float dt){
  if(!animating_||dt<=0) return animating_;
  progress_ += dt*speed_;
  if(progress_>=1.0f){ completeCurrent(); if(playing_&&index_<moves_.size()) { animating_=true; } else { playing_=false; } }
  return animating_;
}
void AnimatedCube::play(){ playing_=true; if(index_<moves_.size()) animating_=true; }
void AnimatedCube::pause(){ playing_=false; }
void AnimatedCube::togglePlay(){ playing_?pause():play(); }
void AnimatedCube::step(){ if(index_<moves_.size()){ if(animating_) completeCurrent(); if(index_<moves_.size()){ animating_=true; playing_=false; } } }
void AnimatedCube::stepBack(){ animating_=false; playing_=false; progress_=0; if(index_==0) return; std::string tok=moves_[index_-1]; Move inv=inverse_move(Move{tok}); auto parsed=parse_facelets(std::string(colors_.begin(),colors_.end())); if(parsed){ CubeState ns=apply(parsed.value(),inv); std::string s=to_facelets(ns); std::copy(s.begin(),s.end(),colors_.begin()); } --index_; }
void AnimatedCube::reset(){ index_=0; progress_=0; animating_=false; playing_=false; }
std::optional<std::string> AnimatedCube::currentToken() const { if(animating_&&index_<moves_.size()) return moves_[index_]; return std::nullopt; }
std::optional<TurnInfo> AnimatedCube::currentTurn() const { auto t=currentToken(); if(!t) return std::nullopt; TurnInfo ti; if(!turnForToken(*t,ti)) return std::nullopt; return ti; }
float AnimatedCube::currentAngleRad() const { auto t=currentTurn(); return t? t->angleRad*progress_:0.0f; }
bool AnimatedCube::isTurningLayer(const Vec3& p) const { auto t=currentTurn(); return t&&isInTurningLayer(p,*t); }
// ---- Renderer GL ----
namespace {
const char* kVS="#version 330 core\nlayout(location=0) in vec3 p; layout(location=1) in vec3 c; uniform mat4 mvp; out vec3 col; void main(){ col=c; gl_Position=mvp*vec4(p,1.0); }";
const char* kFS="#version 330 core\nin vec3 col; out vec4 o; void main(){ o=vec4(col,1.0); }";
unsigned compile(unsigned type,const char* src){ unsigned s=glCreateShader(type); glShaderSource(s,1,&src,nullptr); glCompileShader(s); int ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(!ok){ char buf[1024]; glGetShaderInfoLog(s,1024,nullptr,buf); std::cerr<<"shader: "<<buf<<"\n"; } return s; }
std::array<float,3> colorFor(char ch){ switch(ch){case 'U':return{0.95f,0.95f,0.95f};case 'R':return{0.85f,0.10f,0.10f};case 'F':return{0.10f,0.65f,0.20f};case 'D':return{0.95f,0.85f,0.10f};case 'L':return{1.0f,0.55f,0.05f};case 'B':return{0.10f,0.25f,0.95f};default:return{0.4f,0.4f,0.4f};} }

} // namespace
Renderer::~Renderer(){ shutdown(); }
bool Renderer::init(int w,int h,const char* title){
  width_=w; height_=h;
  if(!glfwInit()){ std::cerr<<"renderer: glfwInit failed\n"; return false; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3); glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  window_=glfwCreateWindow(w,h,title,nullptr,nullptr);
  if(!window_){ std::cerr<<"renderer: glfwCreateWindow failed (no display?)\n"; glfwTerminate(); return false; }
  glfwMakeContextCurrent(window_); glfwSetWindowUserPointer(window_,this);
  glfwSetMouseButtonCallback(window_,[](GLFWwindow* win,int b,int a,int mod){ (void)mod; auto* r=(Renderer*)glfwGetWindowUserPointer(win); double x=0,y=0; glfwGetCursorPos(win,&x,&y); if(r) r->handleMouseButton(b,a,x,y); });
  glfwSetCursorPosCallback(window_,[](GLFWwindow* win,double x,double y){ auto* r=(Renderer*)glfwGetWindowUserPointer(win); if(r) r->handleMouseMotion(x,y); });
  glfwSetScrollCallback(window_,[](GLFWwindow* win,double xo,double yo){ auto* r=(Renderer*)glfwGetWindowUserPointer(win); if(r) r->handleScroll(xo,yo); });
  return ensureGL();
}
bool Renderer::ensureGL(){
  if(glReady_) return true;
  if(!window_ || !glfwGetCurrentContext()){
    std::cerr<<"renderer: no current GL context (no display?)\n"; return false;
  }
  if(!loadGL()){ std::cerr<<"renderer: failed to load GL core functions\n"; return false; }
  unsigned vs=compile(GL_VERTEX_SHADER,kVS), fs=compile(GL_FRAGMENT_SHADER,kFS);
  program_=glCreateProgram(); glAttachShader(program_,vs); glAttachShader(program_,fs); glLinkProgram(program_);
  int ok=0; glGetProgramiv(program_,GL_LINK_STATUS,&ok); if(!ok){ char buf[1024]; glGetProgramInfoLog(program_,1024,nullptr,buf); std::cerr<<"program: "<<buf<<"\n"; return false; }
  glDeleteShader(vs); glDeleteShader(fs);
  glGenVertexArrays(1,&vao_); glGenBuffers(1,&vbo_);
  glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)(3*sizeof(float)));
  glEnable(GL_DEPTH_TEST); glReady_=true; return true;
}
void Renderer::shutdown(){ if(window_){ glfwDestroyWindow(window_); window_=nullptr; } if(glReady_){ glDeleteVertexArrays(1,&vao_); glDeleteBuffers(1,&vbo_); glDeleteProgram(program_); glReady_=false; } glfwTerminate(); }
bool Renderer::shouldClose() const { return window_? glfwWindowShouldClose(window_):true; }
void Renderer::pollEvents(){ if(window_) glfwPollEvents(); }
void Renderer::setHighlighted(std::vector<int> indices){
  highlight_.clear();
  for(int idx: indices){
    if(0<=idx && idx<54) highlight_.push_back(idx);
  }
}
bool Renderer::isHighlighted(int idx) const {
  for(int v: highlight_) if(v==idx) return true;
  return false;
}
void Renderer::handleMouseButton(int b,int a,double x,double y){
  if(b!=GLFW_MOUSE_BUTTON_LEFT) return;
  if(a==GLFW_PRESS){
    dragging_=true;
    dragActive_=false;
    pressX_=x;
    pressY_=y;
    lastX_=x;
    lastY_=y;
  } else {
    dragging_=false;
    dragActive_=false;
    lastX_=x;
    lastY_=y;
  }
}
void Renderer::handleMouseMotion(double x,double y){
  if(!dragging_) return;
  if(!dragActive_){
    double dist = std::hypot(x-pressX_, y-pressY_);
    // Until the threshold the camera does not move at all, so a click
    // (release below threshold) leaves yaw/pitch untouched. Sub-threshold
    // motion is discarded (last tracks the pointer without rotating).
    if(dist < kDragThresholdPx){
      lastX_=x;
      lastY_=y;
      return;
    }
    dragActive_=true;
    // First active step rotates by the increment since the last
    // suppressed position (not from the press point, to avoid a jump).
  }
  camera_.rotate(float(x-lastX_), float(y-lastY_));
  lastX_=x;
  lastY_=y;
}
void Renderer::handleScroll(double,double yo){ camera_.zoom(std::exp(-float(yo)*0.1f)); }
struct Vert{ float p[3]; float c[3]; };
void Renderer::drawQuads(const FaceletArray& colors,const AnimatedCube* anim){
  std::vector<Vert> verts;
  auto turn = anim? anim->currentTurn() : std::nullopt;
  for(int i=0;i<54;++i){ auto q=sticker_quad(i);
    Vec3 center=q.center, n=q.normal, u=q.uDir, v=q.vDir;
    if(turn&&anim&&anim->isTurningLayer(q.cubiePos)){ float ang=anim->currentAngleRad(); center=rotateVec(center,turn->axis,ang); n=rotateVec(n,turn->axis,ang); u=rotateVec(u,turn->axis,ang); v=rotateVec(v,turn->axis,ang); }
    auto col=colorFor(colors[i]); std::array<float,3> black{0.02f,0.02f,0.02f};
    // Highlight tint: blend toward bright yellow so offending stickers
    // are readable against any sticker colour (replaces the old
    // overlay_copy grey-flash).
    if(isHighlighted(i)){
      col={col[0]*0.4f+0.6f, col[1]*0.4f+0.6f, col[2]*0.4f};
    }
    // black backing (slightly bigger, slightly inside) + colour quad
    for(int pass=0;pass<2;++pass){ float half=pass==0?0.50f:0.46f; Vec3 c2=center - n*(pass==0?0.002f:0.0f); auto cc=pass==0?black:col;
      Vec3 cn[4]={c2-u*half-v*half, c2+u*half-v*half, c2+u*half+v*half, c2-u*half+v*half};
      int idx[6]={0,1,2,0,2,3}; for(int k=0;k<6;++k){ Vert vt{{cn[idx[k]].x,cn[idx[k]].y,cn[idx[k]].z},{cc[0],cc[1],cc[2]}}; verts.push_back(vt); } }
  }
  glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(Vert), verts.data(), GL_DYNAMIC_DRAW);
  glDrawArrays(GL_TRIANGLES,0,(GLsizei)verts.size());
}
void Renderer::render(const FaceletArray& colors){
  if(!window_||!glReady_) return;
  glfwMakeContextCurrent(window_);
  int vw=0,vh=0; glfwGetFramebufferSize(window_,&vw,&vh);
  glViewport(0,0,vw,vh); glClearColor(0.08f,0.09f,0.12f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  glUseProgram(program_); glBindVertexArray(vao_);
  Mat4 mvp = camera_.projectionMatrix(vh>0?float(vw)/float(vh):1.0f) * camera_.viewMatrix();
  glUniformMatrix4fv(glGetUniformLocation(program_,"mvp"),1,GL_FALSE,mvp.m);
  drawQuads(colors,nullptr); glfwSwapBuffers(window_);
}
void Renderer::renderAnimated(const AnimatedCube& a){
  if(!window_||!glReady_) return;
  glfwMakeContextCurrent(window_);
  int vw=0,vh=0; glfwGetFramebufferSize(window_,&vw,&vh);
  glViewport(0,0,vw,vh); glClearColor(0.08f,0.09f,0.12f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  glUseProgram(program_); glBindVertexArray(vao_);
  Mat4 mvp = camera_.projectionMatrix(vh>0?float(vw)/float(vh):1.0f) * camera_.viewMatrix();
  glUniformMatrix4fv(glGetUniformLocation(program_,"mvp"),1,GL_FALSE,mvp.m);
  drawQuads(a.facelets(),&a); glfwSwapBuffers(window_);
}
} // namespace rubik::visualiser
