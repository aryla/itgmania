#ifndef SRC_SYNCSTARTMANAGER_H_
#define SRC_SYNCSTARTMANAGER_H_

#include <string>
#include <arpa/inet.h>

#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "SyncStartScoreKeeper.h"
#include "Song.h"
#include "Course.h"

class SyncStartManager
{
private:
	int socketfd;

	std::string songOrCourseWaitingToBeChangedTo;
	std::string lastBroadcastedSongOrCourse;

	std::string previewSong;
	std::int64_t previewSongStartFrame;
	int previewSongMachinesWaiting;
	bool shouldPreview;

	std::string activeSong;
	std::int64_t activeSongStartFrame;
	int activeSongMachinesWaiting;
	bool shouldStart;

	void broadcastSelectedSongOrCourse(const std::string& songOrCourse);

public:
	SyncStartManager();
	~SyncStartManager();

	bool bShouldStall;

	bool isEnabled() const;

	void broadcastSelectedSong(const Song& song);
	void broadcastSelectedCourse(const Course& course);
	void broadcastReadyToStartSong(const Song& song, std::int64_t startFrame);
	void broadcastReadyToStartCourse(const Course& course, std::int64_t startFrame);
	void broadcastReadyToPreviewSong(const Song& song, std::int64_t startFrame);
	void broadcastReadyToPreviewCourse(const Course& course, std::int64_t startFrame);
	void EndCurrentSong();

	void Update();
	std::string GetSongOrCourseToChangeTo();
	bool AttemptStart(std::int64_t& startFrame);
	bool AttemptPreview(std::int64_t& startFrame);
};

extern SyncStartManager *SYNCMAN;

#endif /* SRC_SYNCSTARTMANAGER_H_ */
