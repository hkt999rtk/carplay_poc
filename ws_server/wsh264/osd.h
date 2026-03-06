#ifndef _REMOTE_OSD_H_
#define _REMOTE_OSD_H_

class RemoteOSD {
    protected:
        int m_cmdCount;
        int m_keyCount;
        std::string m_buf;

    public:
        RemoteOSD();
        virtual ~RemoteOSD();

    protected:
        void startCmd();
        void endCmd();

        void addValue(std::string key, std::string value);
        void addValue(std::string key, int value);

    public:
        std::string getJson();
        void lineWidth(int w);
        void font(int size, const char *fontname);
        void strokeStyle(const char *style);
        void fillStyle(const char *style);
        void fillText(const char *text, int x, int y);
        void rect(int x1, int y1, int x2, int y2);
        void circle(int x, int y, int r);
        void fillRect(int x, int y, int w, int h);
        void beginPath();
        void stroke();
        void fill();
        void globalAlpha(int alpha); // 0~100
};

#endif

