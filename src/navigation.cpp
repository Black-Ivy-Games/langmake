#include "langmake/navigation.hpp"
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace lm {
struct Navigation::Impl {
    bool flat = false;
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    dtCrowd* crowd = nullptr;
    float radius=.3f,height=1.8f;
    std::map<std::string,int> agents;
    std::map<std::string,Vec3> goals;
    Json details;
    ~Impl() { if(crowd)dtFreeCrowd(crowd); if (query) dtFreeNavMeshQuery(query); if (mesh) dtFreeNavMesh(mesh); }
    Vec3 to_nav(Vec3 v) const { return flat ? Vec3{v[0], 0, v[1]} : v; }
    Vec3 from_nav(Vec3 v) const { return flat ? Vec3{v[0], v[2], 0} : v; }
};

Navigation::Navigation(const std::string& dimension, const std::vector<Entity>& entities, const Json& settings)
    : impl_(std::make_unique<Impl>()) {
    auto& out = *impl_;
    out.flat = dimension == "2d";
    if (!settings.is_object()) throw std::runtime_error("navigation settings must be an object");
    auto number = [&](const char* key, float fallback, float minimum, float maximum) {
        const float value = settings.value(key, fallback);
        if (!std::isfinite(value) || value < minimum || value > maximum)
            throw std::runtime_error(std::string("navigation.") + key + " is outside its supported bounds");
        return value;
    };
    rcConfig config{};
    config.cs = number("cell_size", .25f, .05f, 4);
    config.ch = number("cell_height", .1f, .025f, 1);
    config.walkableSlopeAngle = number("max_slope", 45, 0, 80);
    const float agent_height = number("agent_height", 1.8f, .1f, 20);
    const float radius = number("agent_radius", .3f, 0, 10);
    out.radius=std::max(.025f,radius);out.height=agent_height;
    const float climb = number("max_step", .4f, 0, 5);
    config.walkableHeight = static_cast<int>(std::ceil(agent_height / config.ch));
    config.walkableClimb = static_cast<int>(std::floor(climb / config.ch));
    config.walkableRadius = static_cast<int>(std::ceil(radius / config.cs));
    config.maxEdgeLen = 12;
    config.maxSimplificationError = 1.3f;
    config.minRegionArea = 4;
    config.mergeRegionArea = 20;
    config.maxVertsPerPoly = 6;
    config.detailSampleDist = config.cs * 6;
    config.detailSampleMaxError = config.ch;
    std::vector<float> vertices;
    std::vector<int> indices;
    auto box = [&](Vec3 center, Vec3 size) {
        const int base = static_cast<int>(vertices.size() / 3);
        for (const auto& sign : std::array<Vec3, 8>{{{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1},
                                                  {-1,1,-1},{1,1,-1},{1,1,1},{-1,1,1}}})
            for (int axis = 0; axis < 3; ++axis) vertices.push_back(center[axis] + size[axis] * sign[axis] * .5f);
        // Positive-Y top faces, outward side normals. Recast classifies steep faces as obstacles.
        constexpr int triangles[] = {4,7,6,4,6,5, 0,1,2,0,2,3, 0,4,5,0,5,1,
                                     1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
        for (const int index : triangles) indices.push_back(base + index);
    };
    if (out.flat) {
        if (!settings.contains("bounds") || !settings["bounds"].is_array() || settings["bounds"].size() != 4)
            throw std::runtime_error("2D navigation requires bounds [min_x,min_y,max_x,max_y]");
        const auto bounds = settings["bounds"].get<std::array<float, 4>>();
        for (float v : bounds) if (!std::isfinite(v) || std::abs(v) > 100000) throw std::runtime_error("Invalid navigation bounds");
        if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) throw std::runtime_error("Navigation bounds must have positive area");
        box({(bounds[0]+bounds[2])*.5f,-.5f,(bounds[1]+bounds[3])*.5f}, {bounds[2]-bounds[0],1,bounds[3]-bounds[1]});
    }
    for (const auto& entity : entities) {
        if ((entity.body != "static"&&entity.body!="kinematic") || !entity.collision_enabled) continue;
        if (out.flat) box({entity.position[0], agent_height, entity.position[1]}, {entity.size[0],agent_height*2,entity.size[1]});
        else box(entity.position, entity.size);
    }
    if (vertices.empty()) throw std::runtime_error("Navigation needs static walkable geometry");
    rcCalcBounds(vertices.data(), static_cast<int>(vertices.size()/3), config.bmin, config.bmax);
    config.bmin[1] -= config.ch * 2;
    config.bmax[1] += agent_height + config.ch * 2;
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
    if (config.width < 1 || config.height < 1 || static_cast<std::uint64_t>(config.width)*config.height > 4*1024*1024)
        throw std::runtime_error("Navigation grid exceeds 4194304 cells; increase cell_size or reduce geometry extent");
    rcContext context(false);
    std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> field(rcAllocHeightfield(), rcFreeHeightField);
    std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
    std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(rcAllocContourSet(), rcFreeContourSet);
    std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> poly(rcAllocPolyMesh(), rcFreePolyMesh);
    std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)> detail(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
    if (!field || !compact || !contours || !poly || !detail) throw std::runtime_error("Navigation allocation failed");
    auto need = [](bool okay) { if (!okay) throw std::runtime_error("Recast navigation build failed"); };
    need(rcCreateHeightfield(&context,*field,config.width,config.height,config.bmin,config.bmax,config.cs,config.ch));
    std::vector<unsigned char> areas(indices.size()/3, RC_NULL_AREA);
    rcMarkWalkableTriangles(&context,config.walkableSlopeAngle,vertices.data(),static_cast<int>(vertices.size()/3),
                           indices.data(),static_cast<int>(areas.size()),areas.data());
    need(rcRasterizeTriangles(&context,vertices.data(),static_cast<int>(vertices.size()/3),indices.data(),areas.data(),
                              static_cast<int>(areas.size()),*field,config.walkableClimb));
    rcFilterLowHangingWalkableObstacles(&context,config.walkableClimb,*field);
    rcFilterLedgeSpans(&context,config.walkableHeight,config.walkableClimb,*field);
    rcFilterWalkableLowHeightSpans(&context,config.walkableHeight,*field);
    need(rcBuildCompactHeightfield(&context,config.walkableHeight,config.walkableClimb,*field,*compact));
    need(rcErodeWalkableArea(&context,config.walkableRadius,*compact));
    need(rcBuildDistanceField(&context,*compact));
    need(rcBuildRegions(&context,*compact,0,config.minRegionArea,config.mergeRegionArea));
    need(rcBuildContours(&context,*compact,config.maxSimplificationError,config.maxEdgeLen,*contours));
    need(rcBuildPolyMesh(&context,*contours,config.maxVertsPerPoly,*poly));
    need(rcBuildPolyMeshDetail(&context,*poly,*compact,config.detailSampleDist,config.detailSampleMaxError,*detail));
    if (poly->npolys == 0) throw std::runtime_error("Navigation geometry produced no walkable polygons");
    for (int i=0;i<poly->npolys;++i) { poly->areas[i]=0; poly->flags[i]=1; }
    dtNavMeshCreateParams params{};
    params.verts=poly->verts; params.vertCount=poly->nverts; params.polys=poly->polys;
    params.polyAreas=poly->areas; params.polyFlags=poly->flags; params.polyCount=poly->npolys; params.nvp=poly->nvp;
    params.detailMeshes=detail->meshes; params.detailVerts=detail->verts; params.detailVertsCount=detail->nverts;
    params.detailTris=detail->tris; params.detailTriCount=detail->ntris;
    params.walkableHeight=agent_height; params.walkableRadius=radius; params.walkableClimb=climb;
    std::copy(poly->bmin,poly->bmin+3,params.bmin); std::copy(poly->bmax,poly->bmax+3,params.bmax);
    params.cs=config.cs; params.ch=config.ch; params.buildBvTree=true;
    unsigned char* data=nullptr; int bytes=0;
    need(dtCreateNavMeshData(&params,&data,&bytes));
    out.mesh=dtAllocNavMesh(); out.query=dtAllocNavMeshQuery();
    if (!out.mesh || !out.query) { dtFree(data); throw std::runtime_error("Detour allocation failed"); }
    if (dtStatusFailed(out.mesh->init(data,bytes,DT_TILE_FREE_DATA))) { dtFree(data); throw std::runtime_error("Detour mesh initialization failed"); }
    if (dtStatusFailed(out.query->init(out.mesh,4096))) throw std::runtime_error("Detour query initialization failed");
    out.crowd=dtAllocCrowd();if(!out.crowd||!out.crowd->init(256,std::max(1.0f,out.radius),out.mesh))throw std::runtime_error("Detour crowd initialization failed");
    out.details={{"backend","Recast/Detour"},{"polygons",poly->npolys},{"vertices",poly->nverts},{"bytes",bytes},
                 {"grid",{config.width,config.height}},{"dimension",dimension},{"max_path_points",256}};
}
Navigation::~Navigation()=default;
Json Navigation::inspect() const { auto result=impl_->details;result["crowd_agents"]=impl_->agents.size();return result; }
Json Navigation::steer(const Json& orders,float dt){
    auto& data=*impl_;if(!orders.is_object()||orders.size()>256||!std::isfinite(dt)||dt<=0||dt>.1f)throw std::runtime_error("Invalid bounded crowd update");
    for(auto it=data.agents.begin();it!=data.agents.end();){if(!orders.contains(it->first)){data.crowd->removeAgent(it->second);data.goals.erase(it->first);it=data.agents.erase(it);}else++it;}
    Json result=Json::object();
    for(const auto& [id,order]:orders.items()){
        const Vec3 position=data.to_nav(order.at("position").get<Vec3>()),goal=data.to_nav(order.at("goal").get<Vec3>());
        const float speed=order.at("speed").get<float>();
        dtCrowdAgentParams params{};params.radius=data.radius;params.height=data.height;params.maxAcceleration=std::max(8.0f,speed*8);params.maxSpeed=speed;
        params.collisionQueryRange=data.radius*12;params.pathOptimizationRange=data.radius*30;params.separationWeight=2.0f;
        params.updateFlags=DT_CROWD_ANTICIPATE_TURNS|DT_CROWD_OBSTACLE_AVOIDANCE|DT_CROWD_SEPARATION|DT_CROWD_OPTIMIZE_VIS|DT_CROWD_OPTIMIZE_TOPO;
        params.obstacleAvoidanceType=0;
        if(!data.agents.contains(id)){
            const int handle=data.crowd->addAgent(position.data(),&params);
            if(handle<0){result[id]={{"status","unreachable"},{"velocity",{0,0,0}}};continue;}
            data.agents.emplace(id,handle);
        }
        const int handle=data.agents.at(id);data.crowd->updateAgentParameters(handle,&params);
        auto* agent=data.crowd->getEditableAgent(handle);
        // Physics is authoritative. Reconcile the crowd position before steering.
        std::copy(position.begin(),position.end(),agent->npos);
        if(!data.goals.contains(id)||data.goals.at(id)!=goal){
            const float extent[3]={2,4,2};dtPolyRef polygon=0;float projected[3]{};dtQueryFilter filter;
            const auto status=data.query->findNearestPoly(goal.data(),extent,&filter,&polygon,projected);
            if(dtStatusFailed(status)||!polygon||!data.crowd->requestMoveTarget(handle,polygon,projected)){
                result[id]={{"status","unreachable"},{"velocity",{0,0,0}}};continue;
            }
            data.goals[id]=goal;
        }
    }
    data.crowd->update(dt,nullptr);
    for(const auto& [id,handle]:data.agents){if(result.contains(id))continue;const auto* agent=data.crowd->getAgent(handle);
        std::string status=agent->targetState==DT_CROWDAGENT_TARGET_FAILED||agent->state==DT_CROWDAGENT_STATE_INVALID?"unreachable":"ready";
        const Vec3 goal=data.goals.contains(id)?data.goals.at(id):Vec3{0,0,0};
        const float dx=goal[0]-agent->npos[0],dz=goal[2]-agent->npos[2];
        if(status=="ready"&&dx*dx+dz*dz<.15f*.15f)status="arrived";
        Vec3 velocity=data.from_nav({agent->nvel[0],agent->nvel[1],agent->nvel[2]});if(status!="ready")velocity={0,0,0};
        result[id]={{"status",status},{"velocity",velocity},{"neighbors",agent->nneis},{"partial",agent->partial}};
    }
    return result;
}
Json Navigation::path(const Vec3& from, const Vec3& to) const {
    auto& data=*impl_;
    const Vec3 start=data.to_nav(from), end=data.to_nav(to);
    const float extent[3]={2,4,2};
    dtQueryFilter filter;
    dtPolyRef a=0,b=0;
    float near_a[3]{},near_b[3]{};
    auto failed=[](dtStatus value) { return dtStatusFailed(value); };
    if (failed(data.query->findNearestPoly(start.data(),extent,&filter,&a,near_a)) ||
        failed(data.query->findNearestPoly(end.data(),extent,&filter,&b,near_b)) || !a || !b)
        return {{"status","unreachable"},{"points",Json::array()}};
    dtPolyRef path[512]{}; int path_count=0;
    const auto status=data.query->findPath(a,b,near_a,near_b,&filter,path,&path_count,512);
    if (failed(status) || !path_count || path[path_count-1]!=b || dtStatusDetail(status,DT_PARTIAL_RESULT) || dtStatusDetail(status,DT_BUFFER_TOO_SMALL))
        return {{"status","unreachable"},{"points",Json::array()}};
    float points[256*3]{}; unsigned char flags[256]{}; dtPolyRef refs[256]{}; int count=0;
    const auto straight=data.query->findStraightPath(near_a,near_b,path,path_count,points,flags,refs,&count,256);
    if (failed(straight) || dtStatusDetail(straight,DT_BUFFER_TOO_SMALL)) throw std::runtime_error("Navigation path exceeds 256 points");
    Json result={{"status","ready"},{"points",Json::array()}};
    for(int i=0;i<count;++i) result["points"].push_back(data.from_nav({points[i*3],points[i*3+1],points[i*3+2]}));
    return result;
}
}
