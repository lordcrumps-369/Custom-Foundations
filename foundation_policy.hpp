#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace brick {
struct Coordinate { int32_t x, y, z; uint8_t mask = 0; };
struct Point { float x, y; int32_t z; };
struct Segment { Point a, b; };
inline int steps(int id) { return id == 70 ? 2 : id == 71 ? 6 : id == 72 ? 8 : 0; }
struct Selection {
    int runtime_id=-1, script_id=-1;
    int for_commit(int runtime) const {return runtime==runtime_id?script_id:-1;}
};
constexpr float step = 0.1875f;
constexpr float tolerance = 0.0001f;
struct Context {
    int id = -1;
    Coordinate first{}, last{};
    bool diagonal = false;
    static int64_t half_even(int64_t n) {
        int64_t q=n/2; if(n<0 && n%2)--q;
        return n%2 && q%2 ? q+1 : q;
    }
    bool contains(const Coordinate& c) const {
        // The native diagonal iterator derives two more corners (C26200).
        // Include those extrema before adding the existing one-cell margin.
        int64_t minx=std::min(first.x,last.x),maxx=std::max(first.x,last.x);
        int64_t miny=std::min(first.y,last.y),maxy=std::max(first.y,last.y);
        if(diagonal) {
            const int64_t ax=first.x,ay=first.y,bx=last.x,by=last.y;
            const int64_t cx=half_even(ax-by+ay+bx),cy=half_even(ax-bx+ay+by);
            const int64_t dx=half_even(ax-ay+bx+by),dy=half_even(-ax+ay+bx+by);
            minx=std::min(minx,std::min(cx,dx));maxx=std::max(maxx,std::max(cx,dx));
            miny=std::min(miny,std::min(cy,dy));maxy=std::max(maxy,std::max(cy,dy));
        }
        return steps(id) && first.z==0 && last.z==0 && c.z==1 &&
            int64_t(c.x)>=minx-1 && int64_t(c.x)<=maxx+1 &&
            int64_t(c.y)>=miny-1 && int64_t(c.y)<=maxy+1;
    }
};
inline bool low_rise(float rise) {
    return std::isfinite(rise) && rise >= step-tolerance && rise < 4*step-tolerance;
}

// No retained lot pointer or memory-only height map. Identify a connected,
// level floor from current world data and require an actual brick boundary.
// World queries validate tiles/vertices before touching their native storage.
template<class World>
bool short_brick_floor(World& world, const Coordinate& vertex) {
    if (vertex.z != 1 || !world.vertex_valid(vertex)) return false;
    Coordinate ground{vertex.x,vertex.y,0};
    if (!world.vertex_valid(ground)) return false;
    const float plane = world.height(vertex);
    if (!std::isfinite(plane) || !low_rise(plane-world.height(ground))) return false;
    const int min_x=world.min_x(), min_y=world.min_y();
    const int max_x=world.max_x(), max_y=world.max_y();
    const int64_t width64=int64_t(max_x)-min_x+1, height64=int64_t(max_y)-min_y+1;
    if (width64<=0 || height64<=0 || width64*height64>65536) return false;
    const int width=int(width64), height=int(height64);
    std::vector<uint8_t> visited(size_t(width)*height,0);
    std::vector<Coordinate> queue;
    queue.reserve(size_t(width)*height);
    auto add = [&](int x,int y) {
        if (x<min_x || y<min_y || x>max_x || y>max_y) return;
        const size_t index=size_t(x-min_x)*height+(y-min_y);
        if (visited[index]) return;
        visited[index]=1;
        Coordinate tile{x,y,1};
        if (!world.tile_valid(tile) || !world.floor(tile)) return;
        // All four vertices must remain on this plane. Do not cross slopes,
        // different foundation heights, bare ground or an unallocated tile.
        for (int dx=0;dx<=1;++dx) for(int dy=0;dy<=1;++dy) {
            Coordinate corner{x+dx,y+dy,1};
            if (!world.vertex_valid(corner) || std::fabs(world.height(corner)-plane)>tolerance) return;
        }
        queue.push_back(tile);
    };
    for(int dx=-1;dx<=0;++dx) for(int dy=-1;dy<=0;++dy) add(vertex.x+dx,vertex.y+dy);
    for(size_t index=0;index<queue.size();++index) {
        const Coordinate tile=queue[index];
        const float x=float(tile.x), y=float(tile.y);
        const Segment edges[] = {
            {{x,y,0},{x+1,y,0}}, {{x+1,y,0},{x+1,y+1,0}},
            {{x,y+1,0},{x+1,y+1,0}}, {{x,y,0},{x,y+1,0}},
            {{x,y,0},{x+1,y+1,0}}, {{x,y+1,0},{x+1,y,0}}
        };
        for (const auto& edge:edges) if(world.brick_wall(edge)) return true;
        add(tile.x-1,tile.y); add(tile.x+1,tile.y);
        add(tile.x,tile.y-1); add(tile.x,tile.y+1);
    }
    return false;
}

template<class World>
float lower_gap(World& world,const Coordinate& c,float original,const Context& context) {
    if (!std::isfinite(original) || original<=0 || c.z!=1) return original;
    if ((context.id==70 && context.contains(c)) || short_brick_floor(world,c))
        return std::min(original,step);
    return original;
}

struct GroundSample { Coordinate point; float height; };
template<class World>
std::vector<GroundSample> ground_snapshot(World& world) {
    std::vector<GroundSample> samples;
    const int min_x=world.min_x(),min_y=world.min_y(),max_x=world.max_x(),max_y=world.max_y();
    const int64_t width=int64_t(max_x)-min_x+2,height=int64_t(max_y)-min_y+2;
    if(width<=0 || height<=0 || width*height>65536) return samples;
    samples.reserve(size_t(width*height));
    for(int64_t x=min_x;x<=int64_t(max_x)+1;++x)for(int64_t y=min_y;y<=int64_t(max_y)+1;++y) {
        if(x>INT32_MAX || y>INT32_MAX) return {};
        Coordinate point{int32_t(x),int32_t(y),0};
        if(world.vertex_valid(point))samples.push_back({point,world.height(point)});
    }
    return samples;
}
}
