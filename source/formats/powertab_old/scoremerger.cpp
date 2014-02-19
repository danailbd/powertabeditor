/*
  * Copyright (C) 2014 Cameron White
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

#include "scoremerger.h"

#include <score/score.h>
#include <score/utils.h>
#include <score/voiceutils.h>

/// Approximate upper limit on the number of positions in a system.
static const int POSITION_LIMIT = 30;

ScoreMerger::ScoreMerger(Score &dest, Score &guitarScore, Score &bassScore)
    : myDestScore(dest),
      myDestCaret(dest),
      myDestLoc(myDestCaret.getLocation()),
      myGuitarScore(guitarScore),
      myBassScore(bassScore),
      myGuitarState(guitarScore, false),
      myBassState(bassScore, true),
      myNumGuitarStaves(0)
{
}

void ScoreMerger::mergePlayers()
{
    for (const Player &player : myGuitarScore.getPlayers())
        myDestScore.insertPlayer(player);
    for (const Player &player : myBassScore.getPlayers())
        myDestScore.insertPlayer(player);

    for (const Instrument &instrument : myGuitarScore.getInstruments())
        myDestScore.insertInstrument(instrument);
    for (const Instrument &instrument : myBassScore.getInstruments())
        myDestScore.insertInstrument(instrument);
}

static int insertWholeRest(ScoreLocation &dest, ScoreLocation &)
{
    Position wholeRest(dest.getPositionIndex(), Position::WholeNote);
    wholeRest.setRest();
    dest.getVoice().insertPosition(wholeRest);

    // A whole rest should probably span at least a few positions.
    return 8;
}

static int insertMultiBarRest(ScoreLocation &dest, ScoreLocation &, int count)
{
    Position rest(dest.getPositionIndex(), Position::WholeNote);
    rest.setRest();
    rest.setMultiBarRest(count);
    dest.getVoice().insertPosition(rest);

    // A multi-bar rest should probably span at least a few positions.
    return 16;
}

static void getPositionRange(const ScoreLocation &dest, const ScoreLocation &src,
        int &offset, int &left, int &right)
{
    const System &srcSystem = src.getSystem();
    const Barline *srcBar = src.getBarline();
    assert(srcBar);
    const Barline *nextSrcBar = srcSystem.getNextBarline(srcBar->getPosition());
    assert(nextSrcBar);

    offset = dest.getPositionIndex() - srcBar->getPosition();
    if (srcBar->getPosition() != 0)
        --offset;

    left = srcBar->getPosition();
    right = nextSrcBar->getPosition();
}

/// Copy notes from the source bar to the destination.
static int copyNotes(ScoreLocation &dest, ScoreLocation &src)
{
    int offset, left, right;
    getPositionRange(dest, src, offset, left, right);

    auto positions = ScoreUtils::findInRange(src.getVoice().getPositions(),
                                             left, right);

    if (!positions.empty())
    {
        int length = positions.back().getPosition() - left;
        if (left == 0)
            ++length;

        for (const Position &pos : positions)
        {
            Position newPos(pos);
            newPos.setPosition(newPos.getPosition() + offset);
            dest.getVoice().insertPosition(newPos);
        }

        for (const IrregularGrouping *group :
             VoiceUtils::getIrregularGroupsInRange(src.getVoice(), left, right))
        {
            IrregularGrouping newGroup(*group);
            newGroup.setPosition(newGroup.getPosition() + offset);
            dest.getVoice().insertIrregularGrouping(newGroup);
        }

        return length;
    }
    else
        return 0;
}

int ScoreMerger::importNotes(
    ScoreLocation &dest, ScoreLocation &srcLoc, bool bass,
    std::function<int(ScoreLocation &, ScoreLocation &)> action)
{
    System &destSystem = dest.getSystem();
    const System &srcSystem = srcLoc.getSystem();

    int offset, left, right;
    getPositionRange(dest, srcLoc, offset, left, right);

    const int staffOffset = bass ? myNumGuitarStaves : 0;
    int length = 0;

    // Merge the notes for each staff.
    for (int i = 0; i < srcSystem.getStaves().size(); ++i)
    {
        // Ensure that there are enough staves in the destination system.
        if ((!bass && myNumGuitarStaves <= i) ||
            destSystem.getStaves().size() <= i + staffOffset)
        {
            const Staff &srcStaff = srcSystem.getStaves()[i];
            Staff destStaff(srcStaff.getStringCount());
            destStaff.setClefType(srcStaff.getClefType());
            destStaff.setViewType(bass ? Staff::BassView : Staff::GuitarView);
            destSystem.insertStaff(destStaff);

            if (!bass)
                ++myNumGuitarStaves;
        }

        myDestLoc.setStaffIndex(i + staffOffset);
        srcLoc.setStaffIndex(i);

        // Import dynamics.
        // TODO - this should only be done once when expanding multibar rests.
        for (const Dynamic &dynamic : ScoreUtils::findInRange(
                 srcLoc.getStaff().getDynamics(), left, right - 1))
        {
            Dynamic newDynamic(dynamic);
            newDynamic.setPosition(newDynamic.getPosition() + offset);
            dest.getStaff().insertDynamic(newDynamic);
        }

        // Import each voice.
        for (int v = 0; v < Staff::NUM_VOICES; ++v)
        {
            myDestLoc.setVoiceIndex(v);
            srcLoc.setVoiceIndex(v);

            length = std::max(length, action(myDestLoc, srcLoc));
        }
    }

    return length;
}

void ScoreMerger::copyBarsFromSource(Barline &destBar, Barline &nextDestBar)
{
    // Copy a bar from one of the source scores.
    const Barline *srcBar;
    const System *srcSystem;

    if (!myGuitarState.done && !myGuitarState.finishing)
    {
        srcBar = myGuitarState.loc.getBarline();
        srcSystem = &myGuitarState.loc.getSystem();
    }
    else
    {
        srcBar = myBassState.loc.getBarline();
        srcSystem = &myBassState.loc.getSystem();
    }

    assert(srcBar);
    const Barline *nextSrcBar = srcSystem->getNextBarline(srcBar->getPosition());
    assert(nextSrcBar);

    int destPosition = destBar.getPosition();
    destBar = *srcBar;
    // The first bar cannot be the end of a repeat.
    if (destPosition == 0 && destBar.getBarType() == Barline::RepeatEnd)
        destBar.setBarType(Barline::SingleBar);
    destBar.setPosition(destPosition);

    destPosition = nextDestBar.getPosition();
    nextDestBar = *nextSrcBar;
    nextDestBar.setPosition(destPosition);
}

const PlayerChange *ScoreMerger::findPlayerChange(const State &state)
{
    if (state.done || state.finishing)
        return nullptr;

    Score tempScore;
    ScoreLocation temp(tempScore);
    int offset, left, right;

    getPositionRange(temp, state.loc, offset, left, right);
    auto changes = ScoreUtils::findInRange(
        state.loc.getSystem().getPlayerChanges(), left, right - 1);

    return changes.empty() ? nullptr : &changes.front();
}

void ScoreMerger::mergePlayerChanges()
{
    const PlayerChange *guitarChange = findPlayerChange(myGuitarState);
    const PlayerChange *bassChange = findPlayerChange(myBassState);

    if (guitarChange || bassChange)
    {
        PlayerChange change;
        change.setPosition(myDestLoc.getPositionIndex());

        if (guitarChange)
            change = *guitarChange;
        else
        {
            // If there is only a player change in the bass score, carry over
            // the current active players from the guitar score.
            const PlayerChange *activeGuitars = ScoreUtils::getCurrentPlayers(
                myGuitarScore, myGuitarState.loc.getSystemIndex(),
                myGuitarState.loc.getPositionIndex());
            if (activeGuitars)
                change = *activeGuitars;
        }

        if (!bassChange)
        {
            // If there is only a player change in the guitar score, carry over
            // the current active players from the bass score.
            bassChange = ScoreUtils::getCurrentPlayers(
                myBassScore, myBassState.loc.getSystemIndex(),
                myBassState.loc.getPositionIndex());
        }

        // Merge in the bass score's player change and adjust
        // staff/player/instrument numbers.
        if (bassChange)
        {
            for (int i = 0; i < myBassState.loc.getSystem().getStaves().size();
                 ++i)
            {
                for (const ActivePlayer &player :
                     bassChange->getActivePlayers(i))
                {
                    change.insertActivePlayer(
                        myNumGuitarStaves + i,
                        ActivePlayer(myGuitarScore.getPlayers().size() +
                                         player.getPlayerNumber(),
                                     myGuitarScore.getInstruments().size() +
                                         player.getInstrumentNumber()));
                }
            }
        }

        myDestLoc.getSystem().insertPlayerChange(change);
    }
}

void ScoreMerger::merge()
{
    mergePlayers();
    myDestScore.insertSystem(System());

    while (true)
    {
        System &destSystem = myDestLoc.getSystem();
        Barline *destBar = myDestLoc.getBarline();
        assert(destBar);
        Barline nextDestBar;

        // Copy a bar from one of the scores into the destination bar.
        copyBarsFromSource(*destBar, nextDestBar);

        // We will insert the notes at the first position after the barline.
        if (myDestLoc.getPositionIndex() != 0)
            myDestCaret.moveHorizontal(1);

        // We only need special handling for multi-bar rests if both staves are active.
        if (!myGuitarState.done && !myBassState.done)
        {
            myGuitarState.checkForMultibarRest();
            myBassState.checkForMultibarRest();
        }

        int barLength = 0;

        if (myGuitarState.inMultibarRest && myBassState.inMultibarRest)
        {
            // If both scores are in a multi-bar rest, insert a multi-bar rest
            // for the shorter duration of the two.
            const int count = std::min(myGuitarState.multibarRestCount,
                                       myBassState.multibarRestCount);

            auto action = std::bind(insertMultiBarRest, std::placeholders::_1,
                                    std::placeholders::_2, count);
            barLength = importNotes(myDestLoc, myGuitarState.loc, false, action);
            barLength = std::max(barLength,
                                 importNotes(myDestLoc, myBassState.loc, true, action));

            myGuitarState.multibarRestCount -= count;
            myBassState.multibarRestCount -= count;
        }
        else
        {
            for (State *state : {&myGuitarState, &myBassState})
            {
                if (state->done)
                    continue;

                int length = 0;
                // If one state is a multibar rest, but the other is not, keep
                // inserting whole rests. If we've reached the end of a score,
                // keep inserting whole rests until we move onto the next
                // system in the destination score.
                if (state->inMultibarRest || state->finishing)
                {
                    length = importNotes(myDestLoc, state->loc, state->isBass,
                                         insertWholeRest);

                    if (state->inMultibarRest)
                        --state->multibarRestCount;
                }
                else
                {
                    // TODO - this also should copy time signatures, dynamics, etc.
                    length = importNotes(myDestLoc, state->loc, state->isBass,
                                         copyNotes);
                }

                barLength = std::max(barLength, length);
            }
        }

        // Merge any player changes from the scores.
        // TODO - only do this once when expanding multi-bar rests.
        mergePlayerChanges();

        myGuitarState.advance();
        myBassState.advance();

        const int nextBarPos = destBar->getPosition() + barLength + 1;

        // If we're about to move to a new system, transition from finishing to
        // done.
        if (nextBarPos > POSITION_LIMIT)
        {
            myGuitarState.finishIfPossible();
            myBassState.finishIfPossible();
        }

        if ((myGuitarState.done || myGuitarState.finishing) &&
            (myBassState.done || myBassState.finishing))
        {
            break;
        }

        // Create the next bar or move to the next system.
        if (nextBarPos > POSITION_LIMIT)
        {
            Barline &endBar = destSystem.getBarlines().back();

            // Copy over some of the next bar's properties to the end bar.
            if (nextDestBar.getBarType() != Barline::RepeatStart)
                endBar.setBarType(nextDestBar.getBarType());
            endBar.setRepeatCount(nextDestBar.getRepeatCount());
            endBar.setPosition(nextBarPos);

            KeySignature key = nextDestBar.getKeySignature();
            key.setVisible(false);
            endBar.setKeySignature(key);

            TimeSignature time = nextDestBar.getTimeSignature();
            time.setVisible(false);
            endBar.setTimeSignature(time);

            myDestScore.insertSystem(System());
            myNumGuitarStaves = 0;
            myDestCaret.moveSystem(1);
        }
        else
        {
            // On the next iteration we will properly set up the bar.
            Barline barline(nextBarPos, Barline::FreeTimeBar);
            barline.setPosition(nextBarPos);
            destSystem.insertBarline(barline);

            myDestCaret.moveToNextBar();
            destSystem.getBarlines().back().setPosition(nextBarPos + 10);
        }
    }
}

ScoreMerger::State::State(Score &score, bool isBass)
    : caret(score),
      loc(caret.getLocation()),
      isBass(isBass),
      inMultibarRest(false),
      multibarRestCount(0),
      done(false),
      finishing(false)
{
}

void ScoreMerger::State::advance()
{
    if (inMultibarRest && multibarRestCount == 0)
        inMultibarRest = false;

    if (!inMultibarRest && !caret.moveToNextBar())
        finishing = true;
}

void ScoreMerger::State::finishIfPossible()
{
    if (finishing)
    {
        finishing = false;
        done = true;
    }
}

void ScoreMerger::State::checkForMultibarRest()
{
    if (inMultibarRest)
        return;

    const System &system = loc.getSystem();
    const Barline *bar = loc.getBarline();
    const Barline *nextBar = system.getNextBarline(bar->getPosition());

    for (const Staff &staff : system.getStaves())
    {
        for (const Voice &voice : staff.getVoices())
        {
            for (const Position &pos : ScoreUtils::findInRange(
                     voice.getPositions(), bar->getPosition(),
                     nextBar->getPosition()))
            {
                if (pos.hasMultiBarRest())
                {
                    inMultibarRest = true;
                    multibarRestCount = pos.getMultiBarRestCount();
                    return;
                }
            }
        }
    }
}