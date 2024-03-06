#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>

#include "global.h"
#include "SyncStartManager.h"
#include "SongManager.h"
#include "ScreenSelectMusic.h"
#include "PlayerNumber.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "GameState.h"
#include "CommonMetrics.h"

SyncStartManager *SYNCMAN;

#define BUFSIZE 1024
#define PORT 53000
#define MACHINES 2

enum SyncStartOpcode : std::uint8_t
{
	SyncStartSongSelected = 0,
	SyncStartReadyToPreview = 1,
	SyncStartReadyToStart = 2,
};

std::vector<std::string> split(const std::string& str, const std::string& delim)
{
	std::vector<std::string> tokens;
	tokens.reserve(10);
	size_t prev = 0, pos = 0;

	do
	{
		pos = str.find(delim, prev);
		if (pos == std::string::npos) pos = str.length();
		std::string token = str.substr(prev, pos-prev);
		tokens.push_back(token);
		prev = pos + delim.length();
	}
	while (pos < str.length() && prev < str.length());

	return tokens;
}

std::string SongToString(const Song& song)
{
	RString sDir = song.GetSongDir();
	sDir.Replace("\\","/");
	std::vector<RString> bits;
	split(sDir, "/", bits);

	return song.m_sGroupName + '/' + *bits.rbegin();
}

std::string CourseToString(const Course& course)
{
	if( course.m_sPath.empty() )
	{
		return "";
	}

	RString sDir = course.m_sPath;
	sDir.Replace("\\","/");
	std::vector<RString> bits;
	split(sDir, "/", bits);

	return course.m_sGroupName + '/' + *bits.rbegin();
}

SyncStartManager::SyncStartManager():
	socketfd(-1),
	songOrCourseWaitingToBeChangedTo(""),
	lastBroadcastedSongOrCourse(""),

	previewSong(""),
    previewSongStartFrame(0),
	previewSongMachinesWaiting(MACHINES),
	shouldPreview(false),

	activeSong(""),
    activeSongStartFrame(0),
	activeSongMachinesWaiting(MACHINES),
	shouldStart(false),

	bShouldStall(false)
{
	this->socketfd = socket(AF_INET, SOCK_DGRAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	// we need to be able to broadcast through this socket
	int enableOpt = 1;
	if (
		setsockopt(this->socketfd, SOL_SOCKET, SO_BROADCAST, &enableOpt, sizeof(enableOpt)) == -1 ||
		setsockopt(this->socketfd, SOL_SOCKET, SO_REUSEADDR, &enableOpt, sizeof(enableOpt)) == -1 ||
		setsockopt(this->socketfd, SOL_SOCKET, SO_REUSEPORT, &enableOpt, sizeof(enableOpt)) == -1
	)
	{
		return;
	}

	if( bind(this->socketfd, (const struct sockaddr *)&addr, (socklen_t)sizeof(addr)) < 0 )
	{
		return;
	}
}

SyncStartManager::~SyncStartManager()
{
	if( this->socketfd >= 0 )
	{
		shutdown(this->socketfd, SHUT_RDWR);
		close(this->socketfd);
		this->socketfd = -1;
	}
}

bool SyncStartManager::isEnabled() const
{
	return true;
}

static void broadcast(int socketfd, const char* msg, const size_t msg_len)
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = inet_addr("127.255.255.255");

	if( sendto(socketfd, msg, msg_len, 0, (struct sockaddr *) &addr, sizeof(addr)) == -1 )
	{
		LOG->Warn("sendto() returned -1");
	}
}

static void broadcastMessage(int socketfd, const SyncStartOpcode opcode, const std::string message)
{
	char buffer[BUFSIZE];
	buffer[0] = opcode;
	std::size_t length = message.copy(buffer + 1, BUFSIZE - 1, 0);
	broadcast(socketfd, buffer, length + 1);
}

void SyncStartManager::broadcastSelectedSongOrCourse(const std::string& songOrCourse)
{
	if( this->lastBroadcastedSongOrCourse != songOrCourse )
	{
		LOG->Info("Broadcasting SyncStartSongSelected song %s", songOrCourse.c_str());
		broadcastMessage(this->socketfd, SyncStartSongSelected, songOrCourse);
		this->lastBroadcastedSongOrCourse = songOrCourse;
	}
}

void SyncStartManager::broadcastSelectedSong(const Song& song)
{
	this->broadcastSelectedSongOrCourse(SongToString(song));
}

void SyncStartManager::broadcastSelectedCourse(const Course& course)
{
	this->broadcastSelectedSongOrCourse(CourseToString(course));
}

static std::int64_t readInt64(char* buffer)
{
	return (std::int64_t)(
		(std::uint64_t(std::uint8_t(buffer[0])) << 56) |
		(std::uint64_t(std::uint8_t(buffer[1])) << 48) |
		(std::uint64_t(std::uint8_t(buffer[2])) << 40) |
		(std::uint64_t(std::uint8_t(buffer[3])) << 32) |
		(std::uint64_t(std::uint8_t(buffer[4])) << 24) |
		(std::uint64_t(std::uint8_t(buffer[5])) << 16) |
		(std::uint64_t(std::uint8_t(buffer[6])) <<  8) |
		(std::uint64_t(std::uint8_t(buffer[7])) <<  0)
	);
}

static void writeInt64(std::int64_t x, char* buffer)
{
	buffer[0] = (((std::uint64_t)x) >> 56) & 0xff;
	buffer[1] = (((std::uint64_t)x) >> 48) & 0xff;
	buffer[2] = (((std::uint64_t)x) >> 40) & 0xff;
	buffer[3] = (((std::uint64_t)x) >> 32) & 0xff;
	buffer[4] = (((std::uint64_t)x) >> 24) & 0xff;
	buffer[5] = (((std::uint64_t)x) >> 16) & 0xff;
	buffer[6] = (((std::uint64_t)x) >>  8) & 0xff;
	buffer[7] = (((std::uint64_t)x) >>  0) & 0xff;
}

static void broadcastSongOrCourseReady(int socketfd, const std::string& songOrCourse, SyncStartOpcode opcode, std::int64_t startFrame)
{
	LOG->Info("Broadcasting %s frame %ld song \"%s\"",
		opcode == SyncStartReadyToStart ? "SyncStartReadyToStart" : "SyncStartReadyToPreview",
		startFrame,
		songOrCourse.c_str());

	char buffer[BUFSIZE];
	buffer[0] = opcode;
	writeInt64(startFrame, &buffer[1]);
	std::size_t totalLength = 9 + songOrCourse.copy(buffer + 9, BUFSIZE - 9, 0);
	broadcast(socketfd, buffer, totalLength);
}

void SyncStartManager::broadcastReadyToStartSong(const Song& song, std::int64_t startFrame)
{
	this->bShouldStall = true;
	broadcastSongOrCourseReady(this->socketfd, SongToString(song), SyncStartReadyToStart, startFrame);
}

void SyncStartManager::broadcastReadyToStartCourse(const Course& course, std::int64_t startFrame)
{
	broadcastSongOrCourseReady(this->socketfd, CourseToString(course), SyncStartReadyToStart, startFrame);
}

void SyncStartManager::broadcastReadyToPreviewSong(const Song& song, std::int64_t startFrame)
{
	broadcastSongOrCourseReady(this->socketfd, SongToString(song), SyncStartReadyToPreview, startFrame);
}

void SyncStartManager::broadcastReadyToPreviewCourse(const Course& course, std::int64_t startFrame)
{
	broadcastSongOrCourseReady(this->socketfd, CourseToString(course), SyncStartReadyToPreview, startFrame);
}

void SyncStartManager::Update()
{
	char buffer[BUFSIZE];
	struct sockaddr_in remaddr;
	socklen_t addrlen = sizeof(remaddr);

	// loop through packets received
	int received;

	while( true )
	{
		received = recvfrom(this->socketfd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr *) &remaddr, &addrlen);
		if( received <= 0 ) break;

		switch( buffer[0] )
		{

			case SyncStartSongSelected:
			{
				std::string song = std::string(&buffer[1], received - 1);
				LOG->Info("Received SyncStartSongSelected song \"%s\"", song.c_str());
				this->songOrCourseWaitingToBeChangedTo = song;
				this->lastBroadcastedSongOrCourse = song;
				break;
			}

			case SyncStartReadyToPreview:
			{
				std::int64_t startFrame = readInt64(&buffer[1]);
				std::string song = std::string(&buffer[9], received - 9);

				LOG->Info("Received SyncStartReadyToPreview frame %ld song \"%s\"", startFrame, song.c_str());

				if( song != this->previewSong )
				{
					this->shouldPreview = false;
					this->previewSong = song;
					this->previewSongMachinesWaiting = 1;
					this->previewSongStartFrame = startFrame;
				}
				else
				{
					this->previewSongMachinesWaiting = std::max(0, this->previewSongMachinesWaiting - 1);
					this->previewSongStartFrame = std::max(startFrame, this->previewSongStartFrame);
					this->shouldPreview = this->previewSongMachinesWaiting == 0;
				}
				break;
			}

			case SyncStartReadyToStart:
			{
				std::int64_t startFrame = readInt64(&buffer[1]);
				std::string song = std::string(&buffer[9], received - 9);

				LOG->Info("Received SyncStartReadyToStart frame %ld song \"%s\"", startFrame, song.c_str());

				if( song != this->activeSong )
				{
					this->shouldStart = false;
					this->activeSong = song;
					this->activeSongMachinesWaiting = MACHINES - 1;
					this->activeSongStartFrame = startFrame;
				}
				else
				{
					this->activeSongMachinesWaiting = std::max(0, this->activeSongMachinesWaiting - 1);
					this->activeSongStartFrame = std::max(startFrame, this->activeSongStartFrame);
					this->shouldStart = this->activeSongMachinesWaiting == 0;
					if (this->shouldStart) this->bShouldStall = false;
				}
				break;
			}
		}
	}
}

void SyncStartManager::EndCurrentSong()
{
	this->shouldStart = false;
	this->bShouldStall = false;
	this->activeSong = "";
}

std::string SyncStartManager::GetSongOrCourseToChangeTo()
{
	std::string songOrCourse = this->songOrCourseWaitingToBeChangedTo;
	this->songOrCourseWaitingToBeChangedTo = "";
	return songOrCourse;
}

bool SyncStartManager::AttemptPreview(std::int64_t& startFrame)
{
    if (!this->shouldPreview) return false;
	startFrame = this->previewSongStartFrame;
	this->shouldPreview = false;
	return true;
}

bool SyncStartManager::AttemptStart(std::int64_t& startFrame)
{
    if (!this->shouldStart) return false;
	startFrame = this->activeSongStartFrame;
	this->shouldStart = false;
	return true;
}
