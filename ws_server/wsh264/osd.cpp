#include <stdint.h>
#include <stdlib.h>
#include <string>
#include "osd.h"

using namespace std;

RemoteOSD::RemoteOSD()
{
    m_cmdCount = 0;
    m_keyCount = 0;
    m_buf = "{\"type\":\"osd\", \"ver\":\"0.1\",\"serial\":[";
}

RemoteOSD::~RemoteOSD()
{
}

void RemoteOSD::startCmd()
{
    if (m_cmdCount>0) {
        m_buf += ",{";
    } else {
        m_buf += "{";
    }
    m_cmdCount++;
    m_keyCount = 0;
}

void RemoteOSD::endCmd()
{
    m_buf += "}";
}

void RemoteOSD::addValue(string key, string value)
{
    if (m_keyCount > 0) {
        m_buf += ",";
    }
    m_buf += "\"" + key + "\":" + "\"" + value + "\"";
    m_keyCount++;
}

void RemoteOSD::addValue(string key, int value)
{
    if (m_keyCount > 0) {
        m_buf += ",";
    }
    m_buf += "\"" + key + "\":" + to_string(value);
    m_keyCount++;
}

string RemoteOSD::getJson()
{
    return m_buf + "]}";
}

void RemoteOSD::strokeStyle(const char *style)
{
    startCmd();
    addValue("c", "strokeStyle");
    addValue("v", style);
    endCmd();
}

void RemoteOSD::lineWidth(int w)
{
    startCmd();
    addValue("c", "lineWidth");
    addValue("v", w);
    endCmd();
}

void RemoteOSD::fillStyle(const char *style)
{
    startCmd();
    addValue("c", "fillStyle");
    addValue("v", style);
    endCmd();
}

void RemoteOSD::fillText(const char *text, int x, int y)
{
    startCmd();
    addValue("c", "fillText");
    addValue("v", text);
    addValue("x", x);
    addValue("y", y);
    endCmd();
}

void RemoteOSD::rect(int x, int y, int w, int h)
{
    startCmd();
    addValue("c", "rect");
    addValue("x", x);
    addValue("y", y);
    addValue("w", w);
    addValue("h", h);
    endCmd();
}

void RemoteOSD::circle(int x, int y, int r)
{
    startCmd();
    addValue("c", "circle");
    addValue("x", x);
    addValue("y", y);
    addValue("r", r);
}

void RemoteOSD::fillRect(int x, int y, int w, int h)
{
    startCmd();
    addValue("c", "fillRect");
    addValue("x", x);
    addValue("y", y);
    addValue("w", w);
    addValue("h", h);
    endCmd();
}

void RemoteOSD::beginPath()
{
    startCmd();
    addValue("c", "beginPath");
    endCmd();
}

void RemoteOSD::stroke()
{
    startCmd();
    addValue("c", "stroke");
    endCmd();
}

void RemoteOSD::fill()
{
    startCmd();
    addValue("c", "fill");
    endCmd();
}

void RemoteOSD::font(int size, const char *fontName)
{
    startCmd();
    addValue("c", "font");
    addValue("s", size);
    addValue("v", fontName);
    endCmd();
}

void RemoteOSD::globalAlpha(int alpha) 
{
    startCmd();
    addValue("c", "globalAlpha");
    addValue("v", alpha);
    endCmd();
}
