#include "sndfile_stub.h"
#include "engine/filePlayer.h"
#include <cstring>

void DivFilePlayer::fillBlocksNear(int64_t) {}
void DivFilePlayer::collectGarbage(int64_t) {}
float DivFilePlayer::getSampleAt(int64_t, int) { return 0.0f; }
void DivFilePlayer::runCacheThread() {}
size_t DivFilePlayer::getMemUsage() { return 0; }

void DivFilePlayer::mix(float** buf, int chans, unsigned int size) {
  for (int i = 0; i < chans; i++) {
    std::memset(buf[i], 0, size * sizeof(float));
  }
}

int64_t DivFilePlayer::getPos() { return playPos; }
TimeMicros DivFilePlayer::getPosSeconds() { return TimeMicros(0, 0); }
int64_t DivFilePlayer::setPos(int64_t newPos, unsigned int) {
  playPos = newPos;
  return playPos;
}
int64_t DivFilePlayer::setPosSeconds(TimeMicros, unsigned int) { return 0; }
bool DivFilePlayer::isBlockPresent(int64_t) { return false; }
bool DivFilePlayer::setBlockPriority(int64_t, bool) { return false; }
bool DivFilePlayer::isLoaded() { return false; }
bool DivFilePlayer::isPlaying() { return playing; }
void DivFilePlayer::play(unsigned int) { playing = true; }
void DivFilePlayer::stop(unsigned int) { playing = false; }
bool DivFilePlayer::closeFile() {
  playing = false;
  return false;
}
bool DivFilePlayer::loadFile(const char*) {
  lastError = "Audio file streaming not enabled";
  return false;
}
String DivFilePlayer::getLastError() { return lastError; }
const SF_INFO& DivFilePlayer::getFileInfo() { return si; }
void DivFilePlayer::setOutputRate(int rate) {
  if (rate > 0) outRate = rate;
}
float DivFilePlayer::getVolume() { return volume; }
void DivFilePlayer::setVolume(float vol) { volume = vol; }
bool DivFilePlayer::getActive() { return isActive; }
void DivFilePlayer::setActive(bool active) { isActive = active; }

DivFilePlayer::DivFilePlayer()
  : sincTable(nullptr),
    discardBuf(nullptr),
    blocks(nullptr),
    priorityBlock(nullptr),
    numBlocks(0),
    sf(nullptr),
    playPos(0),
    lastWantBlock(0),
    wantBlock(0),
    outRate(44100),
    rateAccum(0),
    volume(0.0f),
    playing(false),
    fileError(false),
    quitThread(false),
    threadHasQuit(false),
    isActive(false),
    pendingPos(0),
    pendingPosOffset(UINT_MAX),
    pendingPlayOffset(UINT_MAX),
    pendingStopOffset(UINT_MAX),
    cacheThread(nullptr) {
  std::memset(&si, 0, sizeof(SF_INFO));
}

DivFilePlayer::~DivFilePlayer() {}

