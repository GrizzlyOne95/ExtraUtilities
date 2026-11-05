#pragma once
#include <array>
#include <cmath>
#include <string_view>
namespace ExtraUtilities::Bz2Hud
{
    struct Point {float x,y;};
    using Triangle=std::array<Point,3>;
    inline std::array<Triangle,4> Marker(float x,float y,float radius,float scale,float time,bool objective)
    {
        std::array<Triangle,4> result{};
        const float angle=time*(objective?.5f:2.f);
        const float c=std::cos(angle),s=std::sin(angle);
        const float width=scale*(objective?3.f:2.f),length=scale*(objective?6.f:8.f);
        const std::array<Point,3> base{{{0,radius},{-width,radius+length},{width,radius+length}}};
        for (int i=0;i<4;++i)
        {
            const float a=i*1.57079632679f,ca=std::cos(a),sa=std::sin(a);
            for (int j=0;j<3;++j)
            {
                const float px=base[j].x*ca-base[j].y*sa,py=base[j].x*sa+base[j].y*ca;
                result[i][j]={x+px*c+py*s,y-px*s+py*c};
            }
        }
        return result;
    }
    inline const char* CommandIcon(std::string_view text)
    {
        if (text=="Recycler") return "iu_recy";
        if (text.find("Attack")!=text.npos) return "iu_attack";
        if (text.find("Follow")!=text.npos) return "iu_follow";
        if (text.find("Go To")!=text.npos || text.find("Goto")!=text.npos) return "iu_gotonav";
        if (text.find("Recycle")!=text.npos) return "iu_recycle";
        if (text.find("Pick")!=text.npos) return "iu_pickup";
        if (text.find("Drop")!=text.npos) return "iu_dropoff";
        if (text.find("Undeploy")!=text.npos) return "iu_undeploy";
        if (text.find("Deploy")!=text.npos || text.find("Build")!=text.npos) return "iu_deploy";
        if (text.find("Repair")!=text.npos || text.find("Ammo")!=text.npos) return "iu_service";
        if (text.find("Cancel")!=text.npos) return "iu_cancel";
        if (text.find("Recycler")!=text.npos) return "iu_recy";
        if (text.find("Factory")!=text.npos) return "iu_fact";
        if (text.find("Armory")!=text.npos) return "iu_armo";
        if (text.find("Constructor")!=text.npos) return "iu_cons";
        return nullptr;
    }
}
