/*
 * Copyright (C) 2020 Cameron White
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FORMATS_GP7_PARSER_H
#define FORMATS_GP7_PARSER_H

#include <pugixml.hpp>
#include <string>
#include <vector>

namespace Gp7
{
/// Contains metadata about the score.
struct ScoreInfo
{
    std::string myTitle;
    std::string mySubtitle;
    std::string myArtist;
    std::string myAlbum;
    std::string myWords;
    std::string myMusic;
    std::string myCopyright;
    std::string myTabber;
    std::string myInstructions;
    std::string myNotices;
};

struct TempoChange
{
    enum class BeatType
    {
        Eighth,
        Quarter,
        QuarterDotted,
        Half,
        HalfDotted
    };

    /// Index of the bar the tempo change occurs in.
    int myBar = -1;
    /// Specifies the location within the bar, from 0 to 1. (e.g. if 0.75 and
    /// in 4/4 time, the tempo change occurs on the last beat).
    double myPosition = 0;
    /// Text to be displayed along with the tempo change.
    std::string myDescription;
    /// Whether to linearly interpolate speed until the next tempo marker.
    bool myIsLinear = false;
    /// Whether the tempo change is visible.
    bool myIsVisible = true;
    /// Tempo in beats per minute.
    int myBeatsPerMinute = -1;
    /// Unit that the beats are specified in.
    BeatType myBeatType = BeatType::Quarter;
};

struct Staff
{
    std::vector<int> myTuning;
    int myCapo = 0;
};

struct Sound
{
    std::string myLabel;
    int myMidiPreset = -1;
};

struct Track
{
    std::string myName;
    // A track typically has one staff, but can have two staves with different
    // tunings.
    std::vector<Staff> myStaves;
    /// There can be multiple sounds (although every staff in the track uses
    /// the same active sound). Automations describe when the sounds are
    /// changed.
    std::vector<Sound> mySounds;
};

/// Container for a Guitar Pro 7 document.
struct Document
{
    ScoreInfo myScoreInfo;
    std::vector<TempoChange> myTempoChanges;
    std::vector<Track> myTracks;
};

/// Parses the score.gpif XML file.
Document parse(const pugi::xml_document &root);

} // namespace Gp7

#endif
