// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
// Read-only native collision-polygon queries; no hooks, shots or damage.
#include "GameObjectInternal.h"
#include "Game/SurfaceRayGeometry.h"
#include "Util/SehGuard.h"
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ExtraUtilities::Lua::GameObject
{
namespace
{
namespace G=SurfaceRayGeometry;
constexpr unsigned MaxParts=256,MaxVertices=65536,MaxFaces=65536,MaxCorners=64;
constexpr size_t MaxTriangles=2000000,MaxEntries=4096;
// Qualified obj76/GEO layout, shared with OpenShim's GOG 2.2.301 mesh-impact reader.
// Simulation-space native part frames require no Ogre render-origin conversion.
constexpr unsigned ObjName=8,ObjFlags=0x14,ObjFrame=0x20,ObjGeometry=0x64,ObjSibling=0x7c,ObjChild=0x80,ObjKind=0x84;
template<class T>T& Field(void* p,unsigned offset){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
struct Part{void* geometry=nullptr;unsigned vertices=0;uint32_t checksum=0;G::Frame world;};
struct Entry{unsigned vertices=0;uint32_t checksum=0;std::unique_ptr<G::TriangleBvh> mesh;};
std::unordered_map<void*,Entry> cache;size_t cachedTriangles=0;
G::Frame ReadFrame(void* p)
{
 G::Frame f;const auto& m=Field<BZR::MAT_3D>(p,ObjFrame);const float* v=&m.right_x;
 for(int i=0;i<3;++i)for(int j=0;j<3;++j)f.r[i][j]=v[i*3+j];
 f.p[0]=m.posit_x;f.p[1]=m.posit_y;f.p[2]=m.posit_z;return f;
}
uint32_t Checksum(void* p,unsigned n)
{
 const auto* v=Field<const uint32_t*>(p,0xc);uint32_t h=n*2654435761u^static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Field<void*>(p,0x14)));
 for(unsigned i=0;i<n*3;i+=(n*3/16)+1)h=(h^v[i])*16777619u;return h^v[n*3-1];
}
unsigned Collect(void* root,Part* out)
{
 struct Item{void* p;G::Frame parent;bool child;};Item stack[MaxParts];void* seen[MaxParts];unsigned top=0,n=0,count=0;stack[top++]={root,{},false};
 while(top)
 {
  const Item item=stack[--top];if(n==MaxParts)return 0;for(unsigned i=0;i<n;++i)if(seen[i]==item.p)return 0;seen[n++]=item.p;
  const auto local=ReadFrame(item.p);const auto world=item.child?G::Compose(local,item.parent):local;
  const int kind=Field<int>(item.p,ObjKind);const char* name=static_cast<const char*>(item.p)+ObjName;
  if(name[3]=='1'&&name[4]=='1'&&(kind==60||kind==65||kind==66||kind==67||kind==68)&&!(Field<DWORD>(item.p,ObjFlags)&1))
  {
   void* geometry=Field<void*>(item.p,ObjGeometry);unsigned vertices=geometry?Field<unsigned>(geometry,4):0;
   if(vertices>=3&&vertices<=MaxVertices&&Field<void*>(geometry,0xc))out[count++]={geometry,vertices,Checksum(geometry,vertices),world};
  }
  void* sibling=item.child?Field<void*>(item.p,ObjSibling):nullptr;void* child=Field<void*>(item.p,ObjChild);
  if(sibling){if(top==MaxParts)return 0;stack[top++]={sibling,item.parent,true};}
  if(child){if(top==MaxParts)return 0;stack[top++]={child,world,true};}
 }
 return count;
}
unsigned SafeCollect(void* p,Part* out){__try{return Collect(p,out);}__except(Seh::Filter(GetExceptionCode())){return 0;}}
bool ReadGeometry(void* p,std::vector<G::Vec3>& vertices,std::vector<uint32_t>& triangles)
{
 const unsigned n=Field<unsigned>(p,4);if(n<3||n>MaxVertices)return false;const float* v=Field<const float*>(p,0xc);vertices.resize(n);
 for(unsigned i=0;i<n;++i){G::Vec3 a{v[i*3],v[i*3+1],v[i*3+2]};if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z))return false;vertices[i]=a;}
 unsigned count=0;int32_t corners[MaxCorners];
 for(void* f=Field<void*>(p,0x14);f;f=Field<void*>(f,0x30))
 {
  if(++count>MaxFaces)return false;const unsigned size=Field<unsigned>(f,4);if(size<3)continue;if(size>MaxCorners)return false;
  for(unsigned i=0;i<size;++i)corners[i]=Field<int32_t>(f,0x40+i*0x10);
  if(!G::AppendFan(corners,size,n,triangles)||triangles.size()/3>MaxTriangles)return false;
 }
 return !triangles.empty();
}
bool SafeRead(void* p,std::vector<G::Vec3>& v,std::vector<uint32_t>& t){__try{return ReadGeometry(p,v,t);}__except(Seh::Filter(GetExceptionCode())){return false;}}
const G::TriangleBvh* Mesh(const Part& p)
{
 auto it=cache.find(p.geometry);if(it!=cache.end()&&it->second.vertices==p.vertices&&it->second.checksum==p.checksum)return it->second.mesh.get();
 if(it!=cache.end()){cachedTriangles-=it->second.mesh->TriangleCount();cache.erase(it);}
 if(cache.size()>=MaxEntries||cachedTriangles>=MaxTriangles)return nullptr;
 std::vector<G::Vec3> vertices;std::vector<uint32_t> triangles;if(!SafeRead(p.geometry,vertices,triangles)||triangles.size()/3>MaxTriangles-cachedTriangles)return nullptr;
 auto bvh=std::make_unique<G::TriangleBvh>();if(!bvh->Build(std::move(vertices),std::move(triangles)))return nullptr;
 cachedTriangles+=bvh->TriangleCount();Entry e{p.vertices,p.checksum,std::move(bvh)};const auto* result=e.mesh.get();cache.emplace(p.geometry,std::move(e));return result;
}
void* Root(BZR::GameObject* owner){__try{return owner?owner->obj:nullptr;}__except(Seh::Filter(GetExceptionCode())){return nullptr;}}
bool Cast(BZR::handle h,const BZR::VECTOR_3D& from,const BZR::VECTOR_3D& to,G::Hit& best)
{
 auto* owner=BZR::GameObject::GetObj(h);void* root=Root(owner);if(!root)return false;
 Part parts[MaxParts];const unsigned n=SafeCollect(root,parts);bool found=false;best.t=1;
 const double origin[3]={from.x,from.y,from.z};const G::Vec3 direction{to.x-from.x,to.y-from.y,to.z-from.z};
 for(unsigned i=0;i<n;++i){const auto* mesh=Mesh(parts[i]);G::Hit hit;if(mesh&&G::CastPart(*mesh,parts[i].world,origin,direction,best.t,hit)&&(!found||hit.t<best.t)){best=hit;found=true;}}
 return found;
}
}
void ResetObjectSurfaceRaycast() noexcept{cache.clear();cachedTriangles=0;}
int RaycastObjectSurface(lua_State* L)
{
 const auto handle=CheckHandle(L,1);const auto from=CheckVectorOrSingles(L,2);const auto to=CheckVectorOrSingles(L,3);
 if(!RuntimeGate::IsSupported()){lua_pushnil(L);return 1;}
 const double dx=double(to.x)-from.x,dy=double(to.y)-from.y,dz=double(to.z)-from.z;const double length=std::sqrt(dx*dx+dy*dy+dz*dz);
 if(!std::isfinite(length)||length<1e-6||length>10000)return luaL_argerror(L,3,"ray segment must be finite and between 0 and 10000 metres");
 G::Hit hit;const bool found=Seh::CatchCpp("RaycastObjectSurface",[&]{return Cast(handle,from,to,hit);},false);if(!found){lua_pushnil(L);return 1;}
 PushVector(L,BZR::VECTOR_3D{from.x+float(dx*hit.t),from.y+float(dy*hit.t),from.z+float(dz*hit.t)});PushVector(L,BZR::VECTOR_3D{hit.normal.x,hit.normal.y,hit.normal.z});lua_pushnumber(L,length*hit.t);return 3;
}
}
