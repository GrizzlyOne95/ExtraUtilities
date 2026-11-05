#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
    // BZ2's COMMANDTEXT color group (bzgame_init.cfg), opaque RGB, indexed by
    // selected|highlit<<1|disabled<<2.
    struct CommandRowColors {uint32_t background,gradient;};
    constexpr uint32_t Rgb(uint32_t r,uint32_t g,uint32_t b) {return r<<16|g<<8|b;}
    inline CommandRowColors RowColors(bool selected,bool highlit,bool disabled)
    {
        static constexpr std::array<CommandRowColors,8> table{{
            {Rgb(0,127,255),Rgb(0,63,127)},{Rgb(255,0,127),Rgb(127,0,63)},
            {Rgb(127,191,255),Rgb(63,95,127)},{Rgb(255,127,191),Rgb(127,63,95)},
            {Rgb(63,63,63),Rgb(31,31,31)},{Rgb(127,127,127),Rgb(63,63,63)},
            {Rgb(127,127,127),Rgb(63,63,63)},{Rgb(255,255,255),Rgb(127,127,127)}}};
        return table[(selected?1:0)|(highlit?2:0)|(disabled?4:0)];
    }
    inline uint32_t LerpColor(uint32_t a,uint32_t b,float t)
    {
        uint32_t result=0;
        for(int shift=0;shift<24;shift+=8)
        {
            const float from=static_cast<float>((a>>shift)&255),to=static_cast<float>((b>>shift)&255);
            result|=static_cast<uint32_t>(from+(to-from)*t+.5f)<<shift;
        }
        return result;
    }
    // Horizontal inset of a rounded end on one scanline. colorize.dds ends its
    // bars in a half circle of the bar's height.
    inline int RoundedInset(int line,int height)
    {
        const float radius=height*.5f,offset=radius-(line+.5f);
        return static_cast<int>(radius-std::sqrt(std::max(0.f,radius*radius-offset*offset))+.5f);
    }
}
