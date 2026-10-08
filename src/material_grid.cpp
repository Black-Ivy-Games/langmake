#include "langmake/material_grid.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace lm {
MaterialGrid::MaterialGrid(int width,int height,int samples):w(width),h(height),scale(samples),n(0),cw(0),ch(0){
    if(w<1||h<1||w>1024||h>1024||int64_t(w)*h>262144||scale<1||scale>8)throw std::runtime_error("Material grid bounds: 1..1024 per axis, 262144 samples, scale 1..8");
    n=w*h;cw=(w+15)/16;ch=(h+15)/16;
    for(auto& f:fields)f.resize(n);std::fill(fields[1].begin(),fields[1].end(),20);
    active.assign(cw*ch,1);next_active=active;dirty_revision.assign(cw*ch,revision);quiet.assign(cw*ch,0);seen.resize(n);
}
bool MaterialGrid::fluid(int m){return m==3||m==10||m==12||m==14||m==15||m==19||m==20||m==21||m==22||m==23;}
bool MaterialGrid::gas(int m){return m==12||m==19||m==20;}
bool MaterialGrid::powder(int m){return m==4||m==5||m==13||m==17||m==18;}
void MaterialGrid::touch(int i){
    const int x=(i%w)/16,y=(i/w)/16;
    for(int yy=std::max(0,y-1);yy<=std::min(ch-1,y+1);++yy)for(int xx=std::max(0,x-1);xx<=std::min(cw-1,x+1);++xx){active[yy*cw+xx]=next_active[yy*cw+xx]=1;quiet[yy*cw+xx]=0;}
    dirty_revision[y*cw+x]=++revision;sleeping=false;
}
void MaterialGrid::set(int f,int i,double value){if(fields[f][i]!=value){fields[f][i]=value;touch(i);}}
void MaterialGrid::put(int i,int m,double t,double u,double b){fields[0][i]=m;fields[1][i]=t;fields[2][i]=u;fields[3][i]=b;touch(i);}
void MaterialGrid::swap(int a,int b){for(auto&f:fields)std::swap(f[a],f[b]);touch(a);touch(b);}
std::string MaterialGrid::pack()const{
    std::string out;out.reserve(n);int run=0;std::array<int,4> last{-1,0,0,0};
    auto emit=[&]{if(run){out+=char(run+32);out+=char(last[0]+32);out+=char(last[1]/64+32);out+=char(last[1]%64+32);out+=char(last[2]+32);out+=char(last[3]+32);}};
    for(int i=0;i<n;++i){std::array<int,4> v{std::clamp(int(std::floor(fields[0][i])),0,63),std::clamp(int(std::floor(fields[1][i]+64)),0,2047),std::clamp(int(std::floor(fields[2][i])),0,63),std::clamp(int(std::floor(fields[3][i])),0,63)};
        if(v==last&&run<90)++run;else{emit();last=v;run=1;}}
    emit();return out;
}
void MaterialGrid::unpack(const std::string& text,int sw,int sh){
    if(sw<1||sh<1||sw>w||sh>h||w%sw||h%sh||text.size()%6)throw std::runtime_error("Invalid material snapshot dimensions");
    int count=0;for(size_t k=0;k<text.size();k+=6){int v[6];for(int j=0;j<6;++j)v[j]=uint8_t(text[k+j])-32;
        if(v[0]<1||v[0]>90||v[1]<0||v[1]>26||v[2]<0||v[2]>31||v[3]<0||v[3]>63||v[4]<0||v[4]>63||v[5]<0||v[5]>63||count+v[0]>sw*sh)throw std::runtime_error("Invalid material snapshot run");
        for(int r=0;r<v[0];++r,++count){int x=count%sw,y=count/sw;for(int yy=0;yy<h/sh;++yy)for(int xx=0;xx<w/sw;++xx)put((y*(h/sh)+yy)*w+x*(w/sw)+xx,v[1],v[2]*64+v[3]-64,v[4],v[5]);}}
    if(count!=sw*sh)throw std::runtime_error("Truncated material snapshot");
}
Json MaterialGrid::counts()const{
    std::array<int,26> counts{};int hot=0,burning=0;double checksum=0;
    for(int i=0;i<n;++i){int m=int(fields[0][i]);counts[std::max(0,m-1)]++;hot+=fields[1][i]>70;burning+=fields[3][i]>0&&(m==16||m==17||m==15);checksum=std::fmod(checksum+(i+1)*(m+fields[2][i]*29+fields[3][i]*1901+fields[1][i]*17),2147483647.);}
    return {{"materials",counts},{"hot",hot},{"burning",burning},{"checksum",checksum}};
}
bool MaterialGrid::line(double x,double y,double tx,double ty,bool endpoint)const{
    double dx=tx-x,dy=ty-y;int steps=std::max(1,int(std::ceil(std::max(std::abs(dx),std::abs(dy))*2)));
    for(int k=1;k<=steps;++k){int i=index(int(std::floor(x+dx*k/steps)),int(std::floor(y+dy*k/steps)));if(i<0||(solid(int(fields[0][i]))&&(!endpoint||k<steps-1)))return false;}return true;
}
int MaterialGrid::pull(int x,int y,int radius,const std::vector<uint8_t>& blocked,std::vector<int>& held,bool hold){
    int root=index(x,y);if(root<0||solid(int(fields[0][root])))return 0;
    std::vector<int> distance(n,-1),queue;queue.reserve(std::min(n,(radius*2+1)*(radius*2+1)));distance[root]=0;queue.push_back(root);
    const int dx[]{1,-1,0,0},dy[]{0,0,1,-1};
    for(size_t k=0;k<queue.size();++k){int i=queue[k],ix=i%w,iy=i/w;if(distance[i]>=radius)continue;for(int d=0;d<4;++d){int xx=ix+dx[d],yy=iy+dy[d],j=index(xx,yy);if(j>=0&&distance[j]<0&&!solid(int(fields[0][j]))&&(xx-x)*(xx-x)+(yy-y)*(yy-y)<=radius*radius){distance[j]=distance[i]+1;queue.push_back(j);}}}
    std::fill(seen.begin(),seen.end(),0);int moves=0;const int tx[]{0,-1,1,0},ty[]{-1,0,0,1};
    for(size_t k=1;k<queue.size();++k){int i=queue[k];if(!fluid(int(fields[0][i]))||seen[i])continue;int best=-1,bd=distance[i];for(int d=0;d<4;++d){int j=index(i%w+tx[d],i/w+ty[d]);if(j>=0&&distance[j]>=0&&distance[j]<bd&&fields[0][j]==0&&!seen[j]&&(blocked.empty()||!blocked[j])){best=j;bd=distance[j];}}if(best>=0){swap(i,best);seen[i]=seen[best]=1;++moves;}}
    held.clear();for(int i=0;i<n;++i){bool suspended=hold&&distance[i]>=0&&fluid(int(fields[0][i]));if(seen[i]||suspended)held.push_back(i);if(suspended)touch(i);}return moves;
}

// classic-chemistry-v1: stable documented material slots, no project/entity names.
int MaterialGrid::step(int tick,const std::vector<uint8_t>& blocked,const std::vector<int>& held,const Json& env,const Json& grate,std::function<bool(int,int)> lift,std::vector<int>& discoveries){
    // Moving actors can free destinations in an otherwise sleeping chunk.
    if(previous_blocked!=blocked){
        for(int i=0;i<n;++i)if((!previous_blocked.empty()&&previous_blocked[i])!=(!blocked.empty()&&blocked[i]))touch(i);
        previous_blocked=blocked;
    }
    auto&m=fields[0];auto&t=fields[1];auto&u=fields[2];auto&b=fields[3];int changes=0;
    auto blockedAt=[&](int i){return !blocked.empty()&&blocked[i];};
    auto discover=[&](int id){if(std::find(discoveries.begin(),discoveries.end(),id)==discoveries.end())discoveries.push_back(id);};
    auto change=[&](int i,int mat,double temp,double wet=0,double burn=0){put(i,mat,temp,wet,burn);++changes;};
    auto exchange=[&](int i,int j){swap(i,j);++changes;};
    bool thermal=!env.is_null(),any=thermal;auto moving=[&](int a){return fluid(a)||powder(a)||a==6||a==7||a==11||a==16;};
    // Recompute sleeping chunks. Boundary writes wake their neighbors; reactive
    // or thermal chunks stay awake even when their visible materials don't move.
    std::fill(next_active.begin(),next_active.end(),0);
    for(int i=0;i<n;++i)if(m[i]){if(t[i]!=20||m[i]==21)thermal=any=true;if(moving(int(m[i])))any=true;}
    sleeping=!any;if(sleeping)return 0;
    bool conduct=thermal&&tick%3==0;const int off[]{-w,-1,1,w};
    int ex1=0,ex2=-1,ey1=0,ey2=-1;double et=20;
    if(!env.is_null()){ex1=env.at("x1");ex2=env.at("x2");ey1=env.at("y1");ey2=env.at("y2");et=env.at("temp");}
    for(int i=0;i<n;++i){int a=int(m[i]),x=i%w,y=i/w;if(!a||!active[(y/16)*cw+x/16])continue;
        if(x>=ex1&&x<=ex2&&y>=ey1&&y<=ey2&&t[i]>et){t[i]-=1;touch(i);}
        if(a==10||a==14||a==21||conduct)for(int o:off){int j=i+o;if(j<0||j>=n||((o==-1||o==1)&&j/w!=y))continue;int z=int(m[j]);
            if(a==10||a==14){
                if(z==2&&u[j]<24){u[j]+=8;change(i,0,20);if(u[j]>=8){change(j,3,20,u[j]);discover(1);}break;}
                else if(z==6){change(j,7,20,8);change(i,0,20);discover(2);break;}
                else if(z==13&&u[i]<12){double salt=u[j]>0?u[j]:4,take=std::min(salt,12-u[i]);u[i]+=take;m[i]=14;touch(i);if(take==salt)change(j,0,20);else{u[j]=salt-take;touch(j);}discover(3);}
                else if((z==16||z==17)&&b[j]>0){t[j]=std::max(20.,t[j]-90);b[j]=0;touch(j);discover(8);}
                else if(z==23){t[j]-=35;t[i]+=25;touch(i);touch(j);discover(10);}
            }else if(a==21&&z==10&&u[i]>=2){double split=std::floor(u[i]/2);u[i]-=split;touch(i);change(j,21,t[j],split);}
            else if(a==21&&z==8&&u[i]>0){u[i]-=1;change(j,20,25,1);if(u[i]==0)m[i]=22;touch(i);discover(4);break;}
            if(z&&conduct&&i<j&&a!=26&&z!=26){double d=std::clamp(std::floor((t[i]-t[j])/10),-18.,18.);if(d){t[i]-=d;t[j]+=d;touch(i);touch(j);}}
        }
    }
    for(int i=0;i<n;++i){int a=int(m[i]);double temp=t[i];if(!a||!(temp!=20||a==11||a==12||a==23||b[i]>0))continue;int x=i%w,y=i/w;
        if(a==10||a==14){
            if(temp<=-u[i]*2&&!blockedAt(i)){if(u[i]>0)for(int o:off){int j=i+o;if(j>=0&&j<n&&(m[j]==10||m[j]==14)&&u[j]+u[i]<=12){u[j]+=u[i];m[j]=14;u[i]=0;touch(j);break;}}change(i,11,temp,u[i]);discover(5);}
            else if(temp>=100){if(a==14&&u[i]>0){int j=index(x,y-1);if(j>=0&&m[j]==0){change(j,12,105);change(i,13,90,u[i]);discover(11);}}else{change(i,12,105);discover(6);}}
        }else if(a==11&&temp>2){change(i,u[i]>0?14:10,5,u[i]);discover(7);}
        else if(a==12){if(tick%4==0){t[i]-=2;touch(i);}if(temp<65){change(i,10,60);discover(12);}}
        else if(a==3&&temp>85&&!blockedAt(i)){int j=index(x,y-1);if(j>=0&&m[j]==0){change(j,12,100);change(i,2,50);discover(13);}}
        else if(a==7&&temp>90){int j=index(x,y-1);if(j>=0&&m[j]==0){change(j,12,105);change(i,6,std::max(90.,temp-100));discover(14);}}
        else if(a==6&&temp>=650){change(i,25,temp);discover(15);}
        else if(a==23){if(tick%3==0){t[i]-=3;touch(i);}if(temp<650&&!blockedAt(i)){change(i,9,temp);discover(16);}}
        else if(a==16||a==17||a==15){
            if(b[i]==0&&temp>(a==15?330:240)){bool air=false;for(int o:off){int j=i+o;if(j>=0&&j<n&&(m[j]==0||m[j]==19))air=true;}if(air){b[i]=1;touch(i);discover(9);}}
            if(b[i]>0){b[i]++;t[i]=std::max(t[i],450.);touch(i);for(int o:off){int j=i+o;if(j>=0&&j<n){if(m[j]==16||m[j]==17||m[j]==15){t[j]+=24;touch(j);}else if(m[j]==0&&tick%6==0)change(j,19,140);}}if(b[i]>=48){change(i,a==16?17:a==17?18:20,120);discover(17);}}
        }
        if(m[i]!=23&&tick%12==0){if(t[i]>20){t[i]--;touch(i);}else if(t[i]<20&&env.is_null()){t[i]++;touch(i);}}
    }
    int gy=-1,gx1=0,gx2=-1;if(!grate.is_null()){gy=grate.at("y");gx1=grate.at("x1");gx2=grate.at("x2");}
    for(int sub=0;sub<scale;++sub){
        std::fill(seen.begin(),seen.end(),0);for(int i:held)if(i>=0&&i<n)seen[i]=1;
        auto attempt=[&](int i,int x,int y,bool isgas){int j=index(x,y);if(j<0||seen[j]||(blockedAt(j)&&!fluid(int(m[i]))))return false;int a=int(m[i]),z=int(m[j]);bool can=z==0;
            if(a==5&&y==gy&&x>=gx1&&x<=gx2)return false;
            if((a==10||a==14||a==21||a==22)&&(z==4||z==5)&&tick%(z==5?2:6)==0){int beyond=index(x,y+1);if(y>i/w&&beyond>=0&&m[beyond]==0&&!seen[beyond]){exchange(i,beyond);seen[beyond]=seen[i]=1;return true;}}
            if(!isgas&&gas(z))can=true;if(powder(a)&&(z==10||z==14||z==15))can=true;if((a==10||a==14)&&z==15)can=true;
            if(can){exchange(i,j);seen[j]=seen[i]=1;return true;}return false;};
        bool right=tick%2==0;
        for(int yy=0;yy<h;++yy){int y=right?h-yy-1:yy;for(int xx=0;xx<w;++xx){int x=right?xx:w-xx-1,i=y*w+x,a=int(m[i]);if(!moving(a)||seen[i]||!active[(y/16)*cw+x/16])continue;
            bool g=gas(a),liquid=fluid(a)&&!g,pow=powder(a),floating=(a==16&&u[i]==0&&b[i]==0)||a==11||a==6||a==7;if(!g&&!liquid&&!pow&&!floating)continue;
            int dy=g?(a==20?1:-1):1,sign=(tick+x+y)%2==0?1:-1;bool go=!((a==3||a==23)&&tick%2==1);
            if(floating){int below=index(x,y+1),above=index(x,y-1);if(below>=0&&(m[below]==10||m[below]==14)){go=false;if((a==16||a==11)&&above>=0&&(m[above]==10||m[above]==14)&&!seen[above]){if(!blockedAt(above)||(lift&&lift(x,y))){exchange(i,above);seen[above]=seen[i]=1;}}}}
            if(go&&(liquid||g||pow)&&x>0&&x<w-1&&y+dy>=0&&y+dy<h){int row=i+dy*w;if(m[row]==a&&m[row+sign]==a&&m[row-sign]==a&&m[i+sign]==a)go=false;}
            if(go&&!attempt(i,x,y+dy,g)&&!attempt(i,x+sign,y+dy,g)&&!attempt(i,x-sign,y+dy,g)&&(liquid||g))attempt(i,x+sign,y,g);
        }}
    }
    // Observe a full 12-tick rule cycle before sleeping unchanged chunks. This
    // includes odd/even motion, porous-grain periods and environmental cooling.
    for(size_t c=0;c<active.size();++c){quiet[c]=next_active[c]?0:std::min(12,int(quiet[c])+1);if(quiet[c]<12)next_active[c]=1;}
    for(int i=0;i<n;++i)if(m[i]&&(t[i]!=20||m[i]==21||b[i]>0))next_active[(i/w/16)*cw+(i%w/16)]=1;
    if(!env.is_null())std::fill(next_active.begin(),next_active.end(),1);
    active=next_active;return changes;
}
}
