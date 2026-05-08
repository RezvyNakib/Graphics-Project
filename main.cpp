#include <GL/glut.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <mmsystem.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// ── Audio via /dev/dsp or synthesized via OpenAL if available ──
// We use a simple pure-C sine-wave thread with ALSA or OSS fallback
// For maximum portability we write raw PCM to /dev/dsp if present,
// otherwise we silently skip audio.

int W = 800, H = 600;

float sunPulse   = 0.0f;
float birdOffset = 0.0f;
float bikeX      = -100.0f;
float riverWave  = 0.0f;
float smokeT     = 0.0f;
float wheelAngle = 0.0f;

// ── Mode flags ──────────────────────────────────────────────
int  nightMode    = 0;
int  bikerStopped = 0;
int  soundOn      = 1;   // toggled by 'j'

// ── Sound thread ────────────────────────────────────────────
static volatile int  audioRunning = 1;
static volatile int* pSoundOn     = &soundOn;

// Gentle wind-chime / nature melody notes (frequencies in Hz)
static const float melody[] = {
    523.25f, 659.25f, 783.99f, 1046.50f,  // C5 E5 G5 C6
    880.00f, 783.99f, 659.25f,  523.25f,
    392.00f, 523.25f, 659.25f,  783.99f,
    1046.50f,880.00f, 659.25f,  523.25f
};
static const int MELODY_LEN = 16;

#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
// Try ALSA sndfile-less approach: write to /dev/dsp
// If unavailable, the thread simply exits gracefully.
#define SAMPLE_RATE 22050
#define BUF_SAMPLES 512

static void* audioThread(void* arg){
    (void)arg;
    int fd = open("/dev/dsp", O_WRONLY);
    if(fd < 0){ return NULL; }

    // Set 8-bit unsigned PCM, mono, 22050 Hz via ioctl
    int fmt = 8; // AFMT_U8
    ioctl(fd, 0xC0045005 /*SNDCTL_DSP_SETFMT*/, &fmt);
    int ch = 1;
    ioctl(fd, 0xC0045006 /*SNDCTL_DSP_CHANNELS*/, &ch);
    int rate = SAMPLE_RATE;
    ioctl(fd, 0xC0045002 /*SNDCTL_DSP_SPEED*/, &rate);

    unsigned char buf[BUF_SAMPLES];
    int noteIdx   = 0;
    long samplePos= 0;
    long noteSamples = SAMPLE_RATE / 2; // each note 0.5 s

    while(audioRunning){
        for(int i = 0; i < BUF_SAMPLES; i++){
            if(*pSoundOn){
                float freq = melody[noteIdx];
                float t    = (float)samplePos / SAMPLE_RATE;
                // Envelope: fade in+out within note
                float env  = sinf((float)M_PI * (samplePos % noteSamples) / noteSamples);
                // Mix two harmonics for a bell/chime timbre
                float s    = 0.45f * sinf(2.f*(float)M_PI*freq*t)
                           + 0.20f * sinf(4.f*(float)M_PI*freq*t) * env
                           + 0.10f * sinf(6.f*(float)M_PI*freq*t) * env * env;
                s *= env;
                buf[i] = (unsigned char)(128 + 100 * s);
            } else {
                buf[i] = 128; // silence
            }
            samplePos++;
            if(samplePos % noteSamples == 0){
                noteIdx = (noteIdx + 1) % MELODY_LEN;
            }
        }
        write(fd, buf, BUF_SAMPLES);
    }
    close(fd);
    return NULL;
}
#else
static void* audioThread(void* arg){ (void)arg; return NULL; }
#endif

// ── Utilities ──────────────────────────────────────────────

void setColor(float r,float g,float b){ glColor3f(r,g,b); }

void fillPoly(float* p,int n){
    glBegin(GL_POLYGON);
    for(int i=0;i<n;i++) glVertex2f(p[2*i],p[2*i+1]);
    glEnd();
}

void fillCircle(float cx,float cy,float r,int seg=60){
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx,cy);
    for(int i=0;i<=seg;i++){
        float a=2.f*(float)M_PI*i/seg;
        glVertex2f(cx+r*cosf(a),cy+r*sinf(a));
    }
    glEnd();
}

void fillEllipse(float cx,float cy,float rx,float ry,int seg=60){
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx,cy);
    for(int i=0;i<=seg;i++){
        float a=2.f*(float)M_PI*i/seg;
        glVertex2f(cx+rx*cosf(a),cy+ry*sinf(a));
    }
    glEnd();
}

void fillRect(float x,float y,float w,float h){
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y);
    glVertex2f(x+w,y+h); glVertex2f(x,y+h);
    glEnd();
}

void drawLine(float x1,float y1,float x2,float y2,float lw=1.5f){
    glLineWidth(lw);
    glBegin(GL_LINES);
    glVertex2f(x1,y1); glVertex2f(x2,y2);
    glEnd();
}

#define ROAD_Y   55.f
#define ROAD_H   58.f
#define GROUND   (ROAD_Y + ROAD_H)

// ── Sky ─────────────────────────────────────────────────────

void drawSky(){
    if(nightMode){
        glBegin(GL_QUADS);
        setColor(0.02f,0.02f,0.12f); glVertex2f(0,H);    glVertex2f(W,H);
        setColor(0.05f,0.05f,0.20f); glVertex2f(W,260);  glVertex2f(0,260);
        glEnd();
        srand(42);
        setColor(1.f,1.f,0.9f);
        for(int i=0;i<120;i++){
            float sx=(float)(rand()%W);
            float sy=260.f+(float)(rand()%(H-260));
            float sr=0.6f+0.5f*sinf(sunPulse*2.f+i);
            fillCircle(sx,sy,sr,8);
        }
    } else {
        glBegin(GL_QUADS);
        setColor(0.42f,0.72f,0.95f); glVertex2f(0,H);    glVertex2f(W,H);
        setColor(0.62f,0.86f,1.0f);  glVertex2f(W,260);  glVertex2f(0,260);
        glEnd();
    }
}

// ── Moon / Sun ─────────────────────────────────────────────

void drawSun(){
    if(nightMode){
        float p=1.f+0.02f*sinf(sunPulse);
        setColor(0.85f,0.90f,0.95f); fillCircle(680,530,34*p);
        setColor(0.05f,0.05f,0.18f); fillCircle(668,536,26*p);
    } else {
        float p=1.f+0.03f*sinf(sunPulse);
        setColor(1.f,1.f,0.55f); fillCircle(680,530,40*p);
        setColor(1.f,0.90f,0.0f); fillCircle(680,530,30*p);
    }
}

// ── Clouds ──────────────────────────────────────────────────

void drawCloud(float cx,float cy,float s){
    if(nightMode) setColor(0.12f,0.14f,0.22f);
    else          setColor(1,1,1);
    fillEllipse(cx,      cy,      50*s,20*s);
    fillEllipse(cx-32*s, cy-4*s,  32*s,17*s);
    fillEllipse(cx+32*s, cy-4*s,  32*s,17*s);
    fillEllipse(cx-15*s, cy+8*s,  22*s,12*s);
    fillEllipse(cx+15*s, cy+8*s,  22*s,12*s);
}

void drawClouds(){
    drawCloud(200,530,1.f);
    drawCloud(460,500,0.78f);
    drawCloud(720,510,0.82f);
}

// ── Birds ───────────────────────────────────────────────────

void drawBird(float x,float y){
    if(nightMode) return;
    setColor(0.18f,0.18f,0.22f);
    glLineWidth(2.2f);
    glBegin(GL_LINE_STRIP);
    glVertex2f(x-9,y); glVertex2f(x-4,y+5);
    glVertex2f(x,  y); glVertex2f(x+4,y+5);
    glVertex2f(x+9,y);
    glEnd();
}

void drawBirds(){
    float o=birdOffset;
    drawBird(175+o,490); drawBird(195+o,498); drawBird(218+o,487);
    drawBird(450+o*0.6f,468); drawBird(468+o*0.6f,475);
    drawBird(740+o*0.4f,472); drawBird(758+o*0.4f,465);
}

// ── Mountains ───────────────────────────────────────────────

void drawOneMountain(float peakX,float peakY,float halfW,float baseY,
                     float r,float g,float b){
    if(nightMode){ r*=0.35f; g*=0.35f; b*=0.42f; }
    setColor(r,g,b);
    float pts[]={ peakX-halfW,baseY, peakX+halfW,baseY, peakX,peakY };
    fillPoly(pts,3);
}

void drawMountains(){
    float baseY=290.f;
    drawOneMountain(480,430,70,baseY,0.22f,0.48f,0.65f);
    drawOneMountain(575,418,70,baseY,0.17f,0.40f,0.58f);
    drawOneMountain(665,428,65,baseY,0.20f,0.52f,0.62f);
    drawOneMountain(750,435,60,baseY,0.15f,0.38f,0.55f);
    drawOneMountain(775,400,75,baseY,0.14f,0.46f,0.20f);
    drawOneMountain(840,415,60,baseY,0.12f,0.40f,0.18f);
}

// ── Ground ──────────────────────────────────────────────────

void drawGround(){
    float dim = nightMode ? 0.35f : 1.0f;
    setColor(0.22f*dim,0.55f*dim,0.18f*dim);
    fillRect(0,0,W,290);
    setColor(0.32f*dim,0.65f*dim,0.24f*dim);
    float g[]={0,0, W,0, W,215, 580,232, 280,242, 0,222};
    fillPoly(g,6);
    setColor(0.40f*dim,0.72f*dim,0.28f*dim);
    fillRect(0,258,W,32);
    setColor(0.48f*dim,0.62f*dim,0.28f*dim);
    fillRect(200,GROUND,600,18);
    setColor(0.38f*dim,0.68f*dim,0.22f*dim);
    fillRect(0,GROUND,200,14);
}

// ── Pond ────────────────────────────────────────────────────

void drawPond(){
    float dim = nightMode ? 0.4f : 1.0f;
    float cx=100.f, cy=200.f, rx=68.f, ry=32.f;
    setColor(0.25f*dim,0.52f*dim,0.18f*dim); fillEllipse(cx,cy,rx+10,ry+8);
    setColor(0.22f*dim,0.48f*dim,0.75f*dim); fillEllipse(cx,cy,rx,ry);
    setColor(0.38f*dim,0.62f*dim,0.88f*dim); fillEllipse(cx,cy,rx*0.65f,ry*0.55f);
    float wy=1.8f*sinf(riverWave);
    setColor(0.55f*dim,0.75f*dim,0.95f*dim);
    drawLine(cx-28,cy+wy,    cx+28,cy+wy,    1.6f);
    drawLine(cx-16,cy-8+wy, cx+16,cy-8+wy,  1.4f);
    drawLine(cx-20,cy+10+wy,cx+20,cy+10+wy, 1.4f);
    setColor(0.28f,0.48f,0.12f);
    drawLine(cx-60,cy+10,cx-58,cy+28,2.f);
    drawLine(cx-56,cy+12,cx-52,cy+30,2.f);
    drawLine(cx+52,cy+8, cx+54,cy+26,2.f);
    drawLine(cx+58,cy+6, cx+62,cy+24,2.f);
}

// ── Road ────────────────────────────────────────────────────

void drawRoad(){
    float dim = nightMode ? 0.5f : 1.0f;
    setColor(0.58f*dim,0.42f*dim,0.24f*dim);
    fillRect(0,ROAD_Y,(float)W,ROAD_H);
    setColor(0.35f*dim,0.22f*dim,0.08f*dim);
    drawLine(0,ROAD_Y,        (float)W,ROAD_Y,        3.f);
    drawLine(0,ROAD_Y+ROAD_H, (float)W,ROAD_Y+ROAD_H, 3.f);
    setColor(0.95f,0.90f,0.45f);
    float dashW=36.f,gap=22.f,cy=ROAD_Y+ROAD_H*0.5f;
    for(float x=0;x<(float)W;x+=dashW+gap)
        drawLine(x,cy,x+dashW,cy,3.f);
}

// ── Chimney Smoke ───────────────────────────────────────────

void drawSmokePuff(float cx,float baseY,float phase){
    float t    = fmodf(smokeT+phase,2.f*(float)M_PI);
    float frac = t/(2.f*(float)M_PI);
    float cy   = baseY+frac*55.f;
    float r    = 5.f+frac*9.f;
    float grey = (nightMode ? 0.30f : 0.72f)+0.10f*sinf(t);
    setColor(grey,grey,grey);
    fillCircle(cx,        cy,         r);
    fillCircle(cx+r*0.5f, cy-r*0.3f, r*0.75f);
    fillCircle(cx-r*0.4f, cy-r*0.4f, r*0.65f);
}
void drawChimneySmoke(float cx,float topY){
    drawSmokePuff(cx,topY,0.0f);
    drawSmokePuff(cx,topY,(float)M_PI);
}

// ── Houses ──────────────────────────────────────────────────

void drawOneHouse(float hx,
                  float wR,float wG,float wB,
                  float rR,float rG,float rB){
    float dim = nightMode ? 0.4f : 1.0f;
    float hy=GROUND, wall_w=95.f, wall_h=88.f;
    float cx=hx+wall_w*0.5f;
    setColor(wR*dim,wG*dim,wB*dim); fillRect(hx,hy,wall_w,wall_h);
    setColor(rR*dim,rG*dim,rB*dim);
    float roof[]={ hx-10,hy+wall_h, hx+wall_w+10,hy+wall_h, cx,hy+wall_h+58 };
    fillPoly(roof,3);
    float chX=cx+18, chBottomY=hy+wall_h+30;
    setColor(rR*0.7f*dim,rG*0.7f*dim,rB*0.7f*dim); fillRect(chX,chBottomY,10,28);
    setColor(0.26f*dim,0.14f*dim,0.05f*dim); fillRect(cx-11,hy,22,46);
    if(nightMode){
        setColor(0.95f,0.80f,0.20f); fillRect(hx+10,hy+wall_h-42,26,22);
        setColor(0.95f,0.80f,0.20f); fillRect(hx+wall_w-36,hy+wall_h-42,26,22);
    } else {
        setColor(0.85f,0.93f,1.0f); fillRect(hx+10,hy+wall_h-42,26,22);
        setColor(0.28f,0.16f,0.06f);
        drawLine(hx+23,hy+wall_h-42,hx+23,hy+wall_h-20,1.8f);
        drawLine(hx+10,hy+wall_h-31,hx+36,hy+wall_h-31,1.8f);
        setColor(0.85f,0.93f,1.0f); fillRect(hx+wall_w-36,hy+wall_h-42,26,22);
        setColor(0.28f,0.16f,0.06f);
        drawLine(hx+wall_w-23,hy+wall_h-42,hx+wall_w-23,hy+wall_h-20,1.8f);
        drawLine(hx+wall_w-36,hy+wall_h-31,hx+wall_w-10,hy+wall_h-31,1.8f);
    }
}

void drawHouses(){
    drawOneHouse(200,0.82f,0.22f,0.14f,0.52f,0.30f,0.12f);
    drawOneHouse(390,0.88f,0.75f,0.22f,0.48f,0.26f,0.08f);
    drawOneHouse(595,0.38f,0.62f,0.72f,0.20f,0.36f,0.52f);
}

void drawAllSmoke(){
    float chTopY = GROUND+88.f+30.f+28.f;
    drawChimneySmoke(265.f,chTopY);
    drawChimneySmoke(660.f,chTopY);
}

// ── Trees ───────────────────────────────────────────────────

void drawTree(float x,float y,float s){
    float dim = nightMode ? 0.35f : 1.0f;
    setColor(0.40f*dim,0.24f*dim,0.10f*dim); fillRect(x-6*s,y,12*s,72*s);
    setColor(0.12f*dim,0.44f*dim,0.14f*dim); fillCircle(x,      y+92*s,40*s);
    setColor(0.16f*dim,0.50f*dim,0.18f*dim);
    fillCircle(x-18*s,y+76*s,30*s);
    fillCircle(x+18*s,y+78*s,28*s);
    setColor(0.22f*dim,0.58f*dim,0.24f*dim); fillCircle(x,y+106*s,33*s);
}

void drawTrees(){
    float base=GROUND;
    drawTree(185,base,0.85f);
    drawTree(358,base,0.88f);
    drawTree(562,base,0.82f);
    drawTree(760,base,0.80f);
}

// ── Wheel helper ───────────────────────────────────────────

void drawSpoke(float cx,float cy,float r,float angle){
    drawLine(cx,cy,cx+r*cosf(angle),cy+r*sinf(angle),1.4f);
}

void drawWheel(float cx,float cy,float r){
    setColor(0.12f,0.12f,0.12f); fillCircle(cx,cy,r);
    setColor(0.70f,0.70f,0.72f); fillCircle(cx,cy,r*0.82f);
    setColor(0.45f,0.45f,0.48f);
    for(int i=0;i<8;i++)
        drawSpoke(cx,cy,r*0.80f, wheelAngle + i*(float)M_PI/4.f);
    setColor(0.25f,0.25f,0.28f); fillCircle(cx,cy,r*0.13f);
}

// ── Bicycle + Rider ─────────────────────────────────────────

void drawBicycleAndRider(float bx, float by){
    const float WR = 22.f;
    float rwX=bx,       rwY=by;
    float fwX=bx+58.f,  fwY=by;
    float bbX=bx+26.f,  bbY=by+19.f;
    float stX=bx+18.f,  stY=by+46.f;
    float htX=bx+54.f,  htY=by+36.f;

    drawWheel(rwX,rwY,WR);
    drawWheel(fwX,fwY,WR);

    setColor(0.80f,0.15f,0.10f);
    drawLine(rwX,rwY,  bbX,bbY, 3.0f);
    drawLine(bbX,bbY,  stX,stY, 3.0f);
    drawLine(bbX,bbY,  htX,htY, 3.0f);
    drawLine(stX,stY,  htX,htY, 3.0f);
    drawLine(htX,htY,  fwX,fwY, 3.0f);
    drawLine(stX,stY,  rwX,rwY, 2.5f);

    setColor(0.35f,0.35f,0.38f);
    drawLine(stX,stY, stX,stY+8.f, 2.5f);
    setColor(0.18f,0.12f,0.06f);
    fillEllipse(stX,stY+8.f,  13.f,3.0f);
    fillEllipse(stX,stY+10.f, 11.f,4.0f);

    setColor(0.35f,0.35f,0.38f);
    drawLine(htX,htY, htX+2.f,htY+10.f, 2.5f);
    drawLine(htX-4.f,htY+10.f, htX+10.f,htY+12.f, 3.0f);

    setColor(0.40f,0.40f,0.44f);
    drawLine(bbX-12.f,bbY-2.f, bbX+12.f,bbY+3.f, 2.5f);
    fillCircle(bbX-12.f,bbY-2.f, 3.5f);
    fillCircle(bbX+12.f,bbY+3.f, 3.5f);
    setColor(0.25f,0.25f,0.28f);
    fillCircle(bbX,bbY,4.2f);

    float hipX = stX,          hipY = stY + 14.f;
    float tLen  = 30.f;
    float tAng  = 0.65f;
    float shouX = hipX + tLen*cosf(tAng);
    float shouY = hipY + tLen*sinf(tAng);
    float handX = htX + 7.f,  handY = htY + 12.f;
    float elbX  = (shouX+handX)*0.5f + 1.f;
    float elbY  = (shouY+handY)*0.5f - 5.f;
    float headR = 8.5f;
    float headX = shouX + 3.f;
    float headY = shouY + 12.f + headR;

    float fPedX=bbX+12.f, fPedY=bbY+3.f;
    float bPedX=bbX-12.f, bPedY=bbY-2.f;

    float fKneeX = hipX*0.40f + fPedX*0.60f + 5.f;
    float fKneeY = hipY - 8.f;
    float bKneeX = hipX*0.45f + bPedX*0.55f - 3.f;
    float bKneeY = hipY - 14.f;

    setColor(0.22f,0.18f,0.52f);
    drawLine(hipX,hipY, bKneeX,bKneeY, 5.5f);
    drawLine(bKneeX,bKneeY, bPedX,bPedY, 5.5f);

    setColor(0.22f,0.48f,0.75f);
    glLineWidth(9.5f);
    glBegin(GL_LINES);
    glVertex2f(hipX,hipY);  glVertex2f(shouX,shouY);
    glEnd();

    setColor(0.22f,0.48f,0.75f);
    drawLine(shouX,shouY, elbX,elbY, 5.5f);
    setColor(0.82f,0.60f,0.44f);
    drawLine(elbX,elbY, handX,handY, 4.5f);

    setColor(0.28f,0.24f,0.62f);
    drawLine(hipX,hipY, fKneeX,fKneeY, 5.5f);
    drawLine(fKneeX,fKneeY, fPedX,fPedY, 5.5f);

    setColor(0.15f,0.10f,0.05f);
    fillEllipse(fPedX+3.f,fPedY,   8.f,3.5f);
    fillEllipse(bPedX-2.f,bPedY,   8.f,3.5f);

    setColor(0.82f,0.60f,0.44f);
    drawLine(shouX+1.f,shouY+2.f, headX,headY-headR, 4.f);

    setColor(0.85f,0.65f,0.48f);
    fillCircle(headX,headY,headR);

    setColor(0.88f,0.20f,0.08f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(headX,headY);
    for(int i=0;i<=20;i++){
        float a=(float)M_PI*i/20.f;
        glVertex2f(headX+(headR+2.5f)*cosf(a),
                   headY+(headR+2.5f)*sinf(a));
    }
    glEnd();
    setColor(0.72f,0.13f,0.05f);
    fillRect(headX-11.f,headY-2.f,24.f,4.f);

    setColor(0.12f,0.08f,0.06f);
    fillCircle(headX+5.f,headY+1.5f,1.6f);
}

void drawBiker(float offsetX){
    float by = ROAD_Y + 22.f;
    drawBicycleAndRider(offsetX, by);
}

// ── HUD ─────────────────────────────────────────────────────

void drawHUD(){
    // Only show biker stopped status and sound status
    float tc = nightMode ? 0.85f : 0.15f;
    setColor(tc, tc, tc);

    // Sound indicator bottom-left
    glRasterPos2f(28.f, 22.f);
    const char* smsg = soundOn ? "♪ Sound: ON  [J]" : "♪ Sound: OFF [J]";
    for(const char* c=smsg; *c; c++) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);

    if(bikerStopped){
        setColor(0.9f,0.15f,0.10f);
        glRasterPos2f(28.f, H-36.f);
        const char* m2 = "[ Biker Stopped ]";
        for(const char* c=m2; *c; c++) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
    }
}

// ── Display ─────────────────────────────────────────────────

void display(){
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    setColor(1,1,1); fillRect(0,0,(float)W,(float)H);
    drawSky();
    drawSun();
    drawClouds();
    drawBirds();
    drawMountains();
    drawGround();
    drawPond();
    drawRoad();
    drawHouses();
    drawAllSmoke();
    drawTrees();
    drawBiker(bikeX);
    drawHUD();
    setColor(0.50f,0.50f,0.50f);
    glLineWidth(3.f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(18,12); glVertex2f(W-18,12);
    glVertex2f(W-18,H-12); glVertex2f(18,H-12);
    glEnd();
    glutSwapBuffers();
}

// ── Timer / animation ───────────────────────────────────────

void timerFunc(int){
    sunPulse   += 0.05f;
    birdOffset += 0.35f;
    if(birdOffset>90) birdOffset=-30;
    riverWave += 0.07f;
    smokeT    += 0.025f;

    if(!bikerStopped){
        bikeX      += 0.75f;
        wheelAngle -= 0.075f;
        if(bikeX>(float)W+60) bikeX=-120.f;
    }

    glutPostRedisplay();
    glutTimerFunc(16,timerFunc,0);
}

// ── Keyboard ────────────────────────────────────────────────

void keyboard(unsigned char key, int, int){
    if(key=='n' || key=='N'){
        nightMode = !nightMode;
    }
    if(key=='j' || key=='J'){
        soundOn = !soundOn;
    }
    if(key==27) {
        audioRunning = 0;
        exit(0);
    }
}

// ── Mouse ───────────────────────────────────────────────────

void mouse(int button, int state, int, int){
    if(button==GLUT_RIGHT_BUTTON && state==GLUT_DOWN){
        bikerStopped = !bikerStopped;
    }
}

// ── Reshape ─────────────────────────────────────────────────

void reshape(int w,int h){
    W=w; H=h;
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0,w,0,h);
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc,char** argv){
    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB);
    glutInitWindowSize(W,H);
    glutInitWindowPosition(100,80);
    glutCreateWindow("Village Landscape - OpenGL/GLUT");
    glClearColor(1,1,1,1);

    // Start audio thread
    pthread_t audioTid;
    pthread_create(&audioTid, NULL, audioThread, NULL);
    pthread_detach(audioTid);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouse);
    glutTimerFunc(0,timerFunc,0);
    sndPlaySound("sound.wav", SND_ASYNC);
    glutMainLoop();
    audioRunning = 0;
    return 0;
}
