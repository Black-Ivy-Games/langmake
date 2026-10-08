// Included in World::Impl. Native material buffers are VM-owned, bounded and
// explicitly snapshotted; they never enter the entity registry.
struct MaterialChannel { std::shared_ptr<MaterialGrid> grid; int field; };
std::vector<std::weak_ptr<MaterialGrid>> material_grids;
std::map<std::string,DynamicSurface> dynamic_surfaces;
struct SurfaceSource { std::weak_ptr<MaterialGrid> grid; uint64_t revision=0; };
std::map<std::string,SurfaceSource> surface_sources;
std::map<std::string,int> state_providers;
bool flushing_state=false;
static MaterialChannel& material_channel(lua_State* vm,int at){return *static_cast<MaterialChannel*>(luaL_checkudata(vm,at,"LangmakeMaterialChannel"));}
static int channel_get(lua_State* vm){auto& c=material_channel(vm,1);int i=luaL_checkinteger(vm,2)-1;if(i<0||i>=c.grid->n){lua_pushnil(vm);return 1;}lua_pushnumber(vm,c.grid->fields[c.field][i]);return 1;}
static int channel_len(lua_State*vm){lua_pushinteger(vm,material_channel(vm,1).grid->n);return 1;}
static int channel_set(lua_State* vm){auto& c=material_channel(vm,1);int i=luaL_checkinteger(vm,2)-1;double v=number(vm,3);
    if(i<0||i>=c.grid->n||std::abs(v)>100000||(c.field==0&&(v<0||v>26||v!=std::floor(v))))luaL_error(vm,"Invalid material sample index/value");c.grid->set(c.field,i,v);return 0;}
static void push_channel(lua_State*vm,std::shared_ptr<MaterialGrid>g,int field){
    new(lua_newuserdatadtor(vm,sizeof(MaterialChannel),[](void*p){static_cast<MaterialChannel*>(p)->~MaterialChannel();})) MaterialChannel{std::move(g),field};
    if(luaL_newmetatable(vm,"LangmakeMaterialChannel")){lua_pushcfunction(vm,channel_get,"index");lua_setfield(vm,-2,"__index");lua_pushcfunction(vm,channel_len,"length");lua_setfield(vm,-2,"__len");lua_pushcfunction(vm,channel_set,"assign");lua_setfield(vm,-2,"__newindex");lua_pushstring(vm,"locked");lua_setfield(vm,-2,"__metatable");}lua_setmetatable(vm,-2);
}
static std::shared_ptr<MaterialGrid> material(lua_State* vm,int at=1){luaL_checktype(vm,at,LUA_TTABLE);lua_getfield(vm,at,"m");auto g=material_channel(vm,-1).grid;lua_pop(vm,1);return g;}
static double field_number(lua_State*vm,int at,const char*key,double fallback=0){lua_getfield(vm,at,key);double n=lua_isnil(vm,-1)?fallback:number(vm,-1);lua_pop(vm,1);return n;}
static void set_number(lua_State*vm,int at,const char*key,double n){lua_pushnumber(vm,n);lua_setfield(vm,at,key);}
static int script_material_new(lua_State*vm){try{int w=luaL_checkinteger(vm,1),h=luaL_checkinteger(vm,2),scale=luaL_optinteger(vm,3,1);auto& self=owner(vm);size_t samples=0;
    auto count_live=[&]{samples=0;for(auto it=self.material_grids.begin();it!=self.material_grids.end();)if(auto g=it->lock()){samples+=g->n;++it;}else it=self.material_grids.erase(it);};
    count_live();
    if(int64_t(w)*h>0&&samples+int64_t(w)*h>1048576){lua_gc(vm,LUA_GCCOLLECT,0);count_live();}
    if(int64_t(w)*h<1||samples+int64_t(w)*h>1048576)throw std::runtime_error("Native material storage exceeds 1048576 live samples");
    auto g=std::make_shared<MaterialGrid>(w,h,scale);self.material_grids.push_back(g);lua_newtable(vm);int q=lua_gettop(vm);
    const char*names[]{"m","t","u","b"};for(int c=0;c<4;++c){push_channel(vm,g,c);lua_setfield(vm,q,names[c]);}set_number(vm,q,"width",w);set_number(vm,q,"height",h);set_number(vm,q,"scale",scale);return 1;
}catch(const std::exception&e){luaL_error(vm,"%s",e.what());}return 0;}
static std::vector<uint8_t> material_blocked(lua_State*vm,int at,int n){std::vector<uint8_t> b;if(lua_isnoneornil(vm,at))return b;luaL_checktype(vm,at,LUA_TTABLE);b.resize(n);lua_pushnil(vm);while(lua_next(vm,at)){int i=lua_tointeger(vm,-2)-1;if(i>=0&&i<n)b[i]=lua_toboolean(vm,-1);lua_pop(vm,1);}return b;}
static int script_material_pack(lua_State*vm){try{auto g=material(vm);auto s=g->pack();lua_pushlstring(vm,s.data(),s.size());return 1;}catch(const std::exception&e){luaL_error(vm,"%s",e.what());}return 0;}
static int script_material_unpack(lua_State*vm){try{auto g=material(vm);size_t n;const char*s=luaL_checklstring(vm,2,&n);int w=luaL_checkinteger(vm,3),h=luaL_checkinteger(vm,4);MaterialGrid staged(g->w,g->h,g->scale);staged.unpack(std::string(s,n),w,h);staged.revision=g->revision+1;std::fill(staged.dirty_revision.begin(),staged.dirty_revision.end(),staged.revision);*g=std::move(staged);return 0;}catch(const std::exception&e){luaL_error(vm,"%s",e.what());}return 0;}
static int script_material_counts(lua_State*vm){try{push_json(vm,material(vm)->counts());return 1;}catch(const std::exception&e){luaL_error(vm,"%s",e.what());}return 0;}
static int script_material_line(lua_State*vm){auto g=material(vm);double c[4];for(int i=0;i<4;++i){c[i]=number(vm,i+2);if(std::abs(c[i])>4096)luaL_error(vm,"Material ray coordinates must be within +/-4096");}lua_pushboolean(vm,g->line(c[0],c[1],c[2],c[3],lua_toboolean(vm,6)));return 1;}
static int script_material_pull(lua_State*vm){try{auto g=material(vm);int x=luaL_checkinteger(vm,2),y=luaL_checkinteger(vm,3),r=luaL_checkinteger(vm,4);if(r<0||r>512)throw std::runtime_error("Pull radius must be 0..512");auto b=material_blocked(vm,5,g->n);std::vector<int> held;int total=0;
    int strength=luaL_optinteger(vm,6,1);if(strength<1||strength>4)throw std::runtime_error("Pull strength must be 1..4");bool hold=lua_toboolean(vm,7);
    for(int k=0;k<g->scale*strength;++k)total+=g->pull(x,y,r,b,held,hold&&k==g->scale*strength-1);
    lua_newtable(vm);for(size_t k=0;k<held.size();++k){lua_pushinteger(vm,held[k]+1);lua_rawseti(vm,-2,int(k+1));}lua_setfield(vm,1,"held");lua_pushinteger(vm,total);return 1;
}catch(const std::exception&e){luaL_error(vm,"%s",e.what());}return 0;}
static int script_material_step(lua_State*vm){try{auto g=material(vm);int tick=int(field_number(vm,1,"tick"));auto b=material_blocked(vm,2,g->n);Json env=lua_isnoneornil(vm,3)?Json():json_argument(vm,3),grate=lua_isnoneornil(vm,4)?Json():json_argument(vm,4);
    std::vector<int> held;lua_getfield(vm,1,"held");if(lua_istable(vm,-1)){for(int i=1;i<=lua_objlen(vm,-1);++i){lua_rawgeti(vm,-1,i);held.push_back(lua_tointeger(vm,-1)-1);lua_pop(vm,1);}}lua_pop(vm,1);
    std::function<bool(int,int)> lift;if(lua_isfunction(vm,5))lift=[&](int x,int y){lua_pushvalue(vm,5);lua_pushnumber(vm,1+double(x)/g->scale);lua_pushnumber(vm,1+double(y)/g->scale);if(lua_pcall(vm,2,1,0)!=LUA_OK)throw std::runtime_error(lua_tostring(vm,-1));bool yes=lua_toboolean(vm,-1);lua_pop(vm,1);return yes;};
    std::vector<int> discovered;int changed=g->step(tick,b,held,env,grate,lift,discovered);lua_getfield(vm,1,"reactions");for(int id:discovered){lua_pushboolean(vm,true);lua_setfield(vm,-2,std::to_string(id).c_str());}lua_pop(vm,1);
    lua_pushboolean(vm,g->sleeping);lua_setfield(vm,1,"static");lua_newtable(vm);lua_setfield(vm,1,"held");lua_pushinteger(vm,changed);return 1;
}catch(const std::exception&e){luaL_error(vm,"%s",e.what());}return 0;}
static int script_material_revision(lua_State*vm){lua_pushnumber(vm,double(material(vm)->revision));return 1;}
static int script_material_effects(lua_State*vm){auto g=material(vm);lua_newtable(vm);int count=0;std::set<int> occupied;
    for(int i=0;i<g->n&&count<24;++i){int m=int(g->fields[0][i]);if(g->fields[3][i]>0&&(m==15||m==16||m==17)){int x=i%g->w,y=i/g->w,cell=(y/g->scale)*(g->w/g->scale)+x/g->scale;if(!occupied.insert(cell).second)continue;lua_newtable(vm);set_number(vm,lua_gettop(vm),"x",1+double(x)/g->scale);set_number(vm,lua_gettop(vm),"y",1+double(y)/g->scale);lua_rawseti(vm,-2,++count);}}return 1;}
static int script_material_surface(lua_State*vm){try{auto g=material(vm);auto&self=owner(vm);std::string id=luaL_checkstring(vm,2);if(!self.index.contains(id))throw std::runtime_error("Surface entity does not exist");
    size_t bytes=size_t(g->n)*16;for(const auto&[key,s]:self.dynamic_surfaces)if(key!=id)bytes+=s.pixels.size()*4;
    if(bytes>16*1024*1024||(!self.dynamic_surfaces.contains(id)&&self.dynamic_surfaces.size()>=16))throw std::runtime_error("Material surfaces exceed 16 MiB or 16 surfaces");auto&surface=self.dynamic_surfaces[id];
    // Surface is presentation-only; recreated from native matter on load.
    auto& source=self.surface_sources[id];
    bool fresh=surface.width!=g->w*2||surface.height!=g->h*2||source.grid.lock()!=g;
    if(!fresh&&source.revision==g->revision)return 0;
    std::array<const SpriteFrame*,27> frames{};luaL_checktype(vm,3,LUA_TTABLE);
    for(int m=1;m<=26;++m){lua_rawgeti(vm,3,m);lua_getfield(vm,-1,"asset");std::string asset=luaL_checkstring(vm,-1);lua_pop(vm,1);lua_getfield(vm,-1,"clip");std::string clip=luaL_checkstring(vm,-1);lua_pop(vm,2);
        auto f=self.compiled.sprites.find(asset);if(f==self.compiled.sprites.end())throw std::runtime_error("Unknown material palette sprite");frames[m]=&f->second->frames.at(sprite_frame_at(*f->second,clip,0));}
    surface.width=g->w*2;surface.height=g->h*2;surface.pixels.resize(size_t(surface.width)*surface.height);
    for(int i=0;i<g->n;++i){int x=i%g->w,y=i/g->w;if(!fresh&&g->dirty_revision[(y/16)*g->cw+x/16]<=source.revision)continue;int m=int(g->fields[0][i]);
        for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx){uint32_t pixel=0;if(m){auto*f=frames[m];int sx=((x%(g->scale))*2+xx)*f->width/(g->scale*2),sy=((y%(g->scale))*2+yy)*f->height/(g->scale*2);auto* p=&f->rgba[(sy*f->width+sx)*4];int r=p[0],green=p[1],blue=p[2],alpha=p[3];
                if(g->fields[1][i]>180){green=int(green*.74);blue=int(blue*.56);}if(MaterialGrid::gas(m)||m==24)alpha=int(alpha*.48);
                if(g->fields[3][i]>0&&(m==15||m==16||m==17)){r=255;green=130+int(g->fields[3][i])%60;blue=40;}
                pixel=uint32_t(r)|(uint32_t(green)<<8)|(uint32_t(blue)<<16)|(uint32_t(alpha)<<24);}
            surface.pixels[(y*2+yy)*surface.width+x*2+xx]=pixel;}}
    source.grid=g;source.revision=g->revision;static std::atomic<uint64_t> surface_serial{0};surface.revision=++surface_serial;return 0;
}catch(const std::exception&e){luaL_error(vm,"%s",e.what());}return 0;}
static int script_state_bind(lua_State*vm){auto&self=owner(vm);if(self.flushing_state)luaL_error(vm,"Cannot bind state from a state provider");auto key=key_argument(vm,1);bool remove=lua_isnoneornil(vm,2);
    if(!remove){luaL_checktype(vm,2,LUA_TFUNCTION);if(!self.state_providers.contains(key)&&self.state_providers.size()>=32)luaL_error(vm,"At most 32 state providers");}
    if(auto i=self.state_providers.find(key);i!=self.state_providers.end()){lua_unref(vm,i->second);self.state_providers.erase(i);}if(!remove)self.state_providers[key]=lua_ref(vm,2);return 0;}
void flush_state_providers(){
    // A failed update may have partially mutated script locals. Observation must
    // retain committed JSON without invoking more authored code until restore.
    if(script_faulted)return;
    if(state_providers.empty()||flushing_state)return;
    if(executing_script)throw std::runtime_error("State providers cannot recursively observe bound state");
    auto*vm=script.get();Json candidate=game_state;flushing_state=true;
    try{for(const auto&[key,ref]:state_providers){lua_getref(vm,ref);protected_call("snapshot",0,1);auto value=json_argument(vm,-1,8*1024*1024,1048576);lua_pop(vm,1);candidate[key]=std::move(value);}if(candidate.dump().size()>8*1024*1024)throw std::runtime_error("Bound game state exceeds 8 MiB");game_state=std::move(candidate);flushing_state=false;}
    catch(...){flushing_state=false;throw;}
}
