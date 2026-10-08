#pragma once
#include "types.hpp"
#include <functional>
#include <memory>
#include <vector>
namespace lm {
// Native bounded cellular material storage. Coordinates in the solver are samples;
// authoring can choose multiple samples per world unit independently of entities.
struct MaterialGrid {
    int w,h,scale,n,cw,ch;
    std::array<std::vector<double>,4> fields;
    std::vector<uint8_t> active,next_active,seen,quiet,previous_blocked;
    std::vector<uint64_t> dirty_revision;
    uint64_t revision=1;
    bool sleeping=false;
    explicit MaterialGrid(int width,int height,int samples=1);
    static bool fluid(int m);
    static bool gas(int m);
    static bool powder(int m);
    static bool solid(int m){return m!=0&&!fluid(m);}
    int index(int x,int y)const{return x>=0&&x<w&&y>=0&&y<h?y*w+x:-1;}
    void touch(int i);
    void set(int field,int i,double value);
    void put(int i,int m,double t=20,double u=0,double b=0);
    void swap(int a,int b);
    std::string pack()const;
    void unpack(const std::string& cells,int source_w,int source_h);
    Json counts()const;
    bool line(double x,double y,double tx,double ty,bool endpoint)const;
    int pull(int x,int y,int radius,const std::vector<uint8_t>& blocked,std::vector<int>& held,bool hold=false);
    int step(int tick,const std::vector<uint8_t>& blocked,const std::vector<int>& held,
        const Json& environment,const Json& grate,std::function<bool(int,int)> lift, std::vector<int>& discoveries);
};
struct DynamicSurface {
    int width=0,height=0;
    uint64_t revision=0;
    std::vector<uint32_t> pixels;
};
}
