#include "GridSurface.h"
#include <cmath>
#include <algorithm>

namespace
{
    constexpr float kVibHz = 5.5f;
    constexpr float kVibMaxSemis = 0.35f;     // ~35 cents at autoVibrato = 1
    constexpr double kVibDelayMs = 350.0;
    constexpr float kVibMoveThreshold = 0.30f; // semitones of drift that cancels vibrato

    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

GridSurface::GridSurface (juce::MidiMessageCollector& c) : midi (c)
{
    setOpaque (true);
    setInterceptsMouseClicks (true, false);
    lastTickMs = nowMs();
    startTimerHz (60);
}

GridSurface::~GridSurface()
{
    stopTimer();
    releaseAllTouches();
}

void GridSurface::setConfig (const Config& c)
{
    Config n = c;
    n.rows = juce::jlimit (1, kMaxRows, n.rows);
    n.cols = juce::jmax (1, n.cols);
    n.bendRangeSemis = juce::jmax (1, n.bendRangeSemis);
    n.rootPc = ((n.rootPc % 12) + 12) % 12;
    n.scaleMask &= 0xFFF;
    n.autoVibrato = juce::jlimit (0.0f, 1.0f, n.autoVibrato);

    // geometry/tuning changes invalidate held notes: release them cleanly first
    const bool geometryChanged = n.rows != cfg.rows || n.cols != cfg.cols || n.baseNote != cfg.baseNote
                                 || n.rowInterval != cfg.rowInterval || n.bendRangeSemis != cfg.bendRangeSemis;
    if (geometryChanged) releaseAllTouches();

    cfg = n;
    repaint();
}

void GridSurface::post (juce::MidiMessage m)
{
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    midi.addMessageToQueue (m);
}

// ----------------------------------------------------------------- scales
bool GridSurface::isChromatic() const
{
    const int m = cfg.scaleMask & 0xFFF;
    return m == 0xFFF || m == 0;
}

bool GridSurface::inScale (int note) const
{
    if (isChromatic()) return true;
    const int pc = (((note - cfg.rootPc) % 12) + 12) % 12;
    return ((cfg.scaleMask >> pc) & 1) != 0;
}

// Continuous magnetic pull: between two neighbouring in-scale pitches the
// position is warped with a blend of identity and smoothstep, so it is
// flat (sticky) right at in-scale notes and steeper in between. Monotonic,
// endpoints fixed => the slide stays continuous.
float GridSurface::applyScalePull (float cellPitch, int row) const
{
    if (isChromatic()) return cellPitch;
    const int base = rowBaseNote (row);
    const float absPitch = (float) base + cellPitch;
    int lo = (int) std::floor (absPitch);
    int hi = lo + 1;
    for (int i = 0; i < 12 && ! inScale (lo); ++i) --lo;
    for (int i = 0; i < 12 && ! inScale (hi); ++i) ++hi;
    if (hi <= lo) return cellPitch;
    const float span = (float) (hi - lo);
    const float u = juce::jlimit (0.0f, 1.0f, (absPitch - (float) lo) / span);
    const float s = u * u * (3.0f - 2.0f * u);
    const float u2 = u + (s - u) * 0.6f;
    return (float) lo + u2 * span - (float) base;
}

void GridSurface::timerCallback()
{
    const double now = nowMs();
    const float dt = juce::jlimit (0.001f, 0.05f, (float) ((now - lastTickMs) * 0.001));
    lastTickMs = now;

    bool active = ! touches.empty() || ! keyChannels.empty();
    for (size_t i = 0; i < vibAmp.size(); ++i)
    {
        if (vibAmp[i] > 0.005f)
        {
            vibAmp[i] *= 0.955f;
            vibPhase[i] += 1.1f;
            active = true;
        }
        else vibAmp[i] = 0.0f;
    }

    for (auto& [id, t] : touches)
    {
        // trails fade when the finger stops moving
        if (! t.trail.empty()) { t.trail.erase (t.trail.begin()); active = true; }

        if (! t.hasVoice || t.v.channel < 2) continue;
        Voice& v = t.v;

        // hammer accent decay
        if (v.accent > 0.0f)
        {
            v.accent *= 0.82f;
            if (v.accent < 0.02f) v.accent = 0.0f;
            applyPressure (v);
        }

        // auto vibrato: fades in after the finger rests, out the moment it moves
        const bool rest = cfg.autoVibrato > 0.0f && (now - v.anchorMs) > kVibDelayMs;
        const float prevEnv = v.vibEnv;
        v.vibEnv = juce::jlimit (0.0f, 1.0f, v.vibEnv + (rest ? dt / 0.5f : -dt / 0.08f));
        if (v.vibEnv > 0.0f || prevEnv > 0.0f)
        {
            v.vibPhase += juce::MathConstants<float>::twoPi * kVibHz * dt;
            if (v.vibPhase > juce::MathConstants<float>::twoPi) v.vibPhase -= juce::MathConstants<float>::twoPi;
            applyBend (v);
        }
    }
    if (active) repaint();
}

// ----------------------------------------------------------------- drawing
void GridSurface::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    const juce::Colour cyan (0xff4de8ff), violet (0xffa07bff), pink (0xffff6bd6);

    // deep glass backdrop
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a0c16), 0, 0, juce::Colour (0xff06070d), 0, h, false));
    g.fillRoundedRectangle (bounds, 16.0f);

    const float cw = cellW(), ch = cellH();
    juce::Path clip; clip.addRoundedRectangle (bounds, 16.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    const bool chromatic = isChromatic();
    const juce::Font nameFont (juce::FontOptions (juce::jlimit (7.0f, 11.0f, juce::jmin (ch * 0.26f, cw * 0.30f)), juce::Font::plain));
    const juce::Font rootFont (juce::FontOptions (juce::jlimit (8.0f, 14.0f, juce::jmin (ch * 0.34f, cw * 0.38f)), juce::Font::bold));

    // cells
    for (int r = 0; r < cfg.rows; ++r)
    {
        const float y = h - (float) (r + 1) * ch;
        for (int c = 0; c < cfg.cols; ++c)
        {
            const int note = rowBaseNote (r) + c;
            const int pc = ((note % 12) + 12) % 12;
            const bool natural = (pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11);
            const bool isRoot = pc == cfg.rootPc;
            const bool inSc = inScale (note);
            auto cell = juce::Rectangle<float> (c * cw, y, cw, ch).reduced (2.5f);

            // light up cells whose note is currently sounding under a finger
            float glow = keyChannels.count (note) != 0 ? 1.0f : 0.0f;   // held computer key
            for (auto& [id, t] : touches)
            {
                const float dx = (t.pos.x - (c + 0.5f) * cw) / cw, dy = (t.pos.y - (y + ch * 0.5f)) / ch;
                glow = juce::jmax (glow, 1.0f - juce::jlimit (0.0f, 1.0f, std::sqrt (dx * dx + dy * dy) * 0.55f));
            }

            juce::uint32 fill;
            if (chromatic) fill = natural ? 0x14ffffff : 0x08ffffff;
            else           fill = inSc ? 0x20ffffff : 0x04ffffff;
            g.setColour (juce::Colour (fill));
            g.fillRoundedRectangle (cell, 9.0f);
            if (isRoot)
            {
                g.setColour (violet.withAlpha (chromatic ? 0.13f : 0.24f));
                g.fillRoundedRectangle (cell, 9.0f);
            }
            if (glow > 0.0f)
            {
                g.setColour (cyan.withAlpha (0.28f * glow * glow));
                g.fillRoundedRectangle (cell, 9.0f);
            }
            g.setColour (juce::Colour (glow > 0.2f ? 0x30ffffff : (isRoot ? 0x24ffffff : 0x10ffffff)));
            g.drawRoundedRectangle (cell, 9.0f, 1.0f);

            if (inSc)
            {
                if (isRoot)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.8f));
                    g.setFont (rootFont);
                    g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 4),
                                cell.reduced (6.0f, 4.0f), juce::Justification::bottomLeft);
                }
                else
                {
                    g.setColour (juce::Colours::white.withAlpha (chromatic ? 0.30f : 0.50f));
                    g.setFont (nameFont);
                    g.drawText (juce::MidiMessage::getMidiNoteName (note, true, false, 4),
                                cell.reduced (6.0f, 4.0f), juce::Justification::bottomLeft);
                }
            }
        }
    }

    // strings
    for (int r = 0; r < cfg.rows; ++r)
    {
        const float y = h - (float) (r + 1) * ch;
        const float y0 = y + ch * 0.5f;
        const float thick = 1.2f + 0.35f * (float) (cfg.rows - r);
        const Touch* held = nullptr;
        for (auto& [id, t] : touches)
            if (t.row == r && (held == nullptr || (t.inStack && ! rowStack[(size_t) r].empty() && rowStack[(size_t) r].back() == id)))
                held = &t;

        float pressure = 0.5f;
        if (held != nullptr)
            if (auto* vt = voiceTouch (*held))
                pressure = juce::jlimit (0.0f, 1.0f, vt->v.basePressure + vt->v.accent * 0.5f);

        juce::Path p;
        p.startNewSubPath (0.0f, y0);
        if (held != nullptr)
        {
            const float tx = held->pos.x;
            const float ty = juce::jlimit (y0 - ch * 0.45f, y0 + ch * 0.45f, held->pos.y);
            p.cubicTo (tx * 0.5f, y0, tx * 0.6f, ty, tx, ty);
            p.cubicTo (tx + (w - tx) * 0.4f, ty, tx + (w - tx) * 0.5f, y0, w, y0);
        }
        else if (vibAmp[(size_t) r] > 0.0f)
        {
            const float a = vibAmp[(size_t) r] * ch * 0.32f, ph = vibPhase[(size_t) r];
            for (float x = 0.0f; x <= w; x += 5.0f)
            {
                const float u = x / w;
                p.lineTo (x, y0 + a * std::sin (ph) * std::sin (juce::MathConstants<float>::pi * u)
                               + 0.35f * a * std::sin (ph * 2.0f) * std::sin (2.0f * juce::MathConstants<float>::pi * u));
            }
        }
        else p.lineTo (w, y0);

        const bool lit = held != nullptr || vibAmp[(size_t) r] > 0.0f;
        const float bright = held != nullptr ? 0.55f + 0.45f * pressure : 1.0f;
        auto grad = [&] (float alpha)
        {
            return juce::ColourGradient (cyan.withAlpha (alpha), 0.0f, 0.0f, pink.withAlpha (alpha), w, 0.0f, false);
        };
        if (lit)
        {
            g.setGradientFill (grad (0.20f * bright));
            g.strokePath (p, juce::PathStrokeType (thick + 9.0f + 5.0f * (held != nullptr ? pressure : 0.0f),
                                                   juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (grad (0.30f * bright));
            g.strokePath (p, juce::PathStrokeType (thick + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setGradientFill (grad (lit ? juce::jmin (1.0f, 0.55f + 0.45f * bright) : 0.42f));
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // finger trails + touch orbs + bend feedback
    for (auto& [id, t] : touches)
    {
        if (t.trail.size() > 1)
        {
            for (size_t i = 1; i < t.trail.size(); ++i)
            {
                const float k = (float) i / (float) t.trail.size();
                g.setColour (violet.withAlpha (0.55f * k * k));
                g.drawLine (juce::Line<float> (t.trail[i - 1], t.trail[i]), 1.0f + 5.0f * k);
            }
        }
        juce::ColourGradient glow (cyan.withAlpha (0.60f), t.pos.x, t.pos.y,
                                   violet.withAlpha (0.0f), t.pos.x + 60.0f, t.pos.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (t.pos.x - 60.0f, t.pos.y - 60.0f, 120.0f, 120.0f);
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.fillEllipse (t.pos.x - 9.0f, t.pos.y - 9.0f, 18.0f, 18.0f);
        g.setColour (cyan);
        g.drawEllipse (t.pos.x - 17.0f, t.pos.y - 17.0f, 34.0f, 34.0f, 1.5f);

        const Touch* vt = voiceTouch (t);
        if (vt == nullptr || vt->v.channel < 2) continue;

        // bend ruler: from the plucked note's cell centre to the finger, tick per semitone
        const float cellOrigin = (float) (vt->v.noteNumber - rowBaseNote (t.row));
        const float bendSemis = t.pitchNow - cellOrigin;
        const float originX = (cellOrigin + 0.5f) * cw;
        const float rulerY = h - (float) (t.row + 1) * ch + ch * 0.88f;
        if (std::abs (bendSemis) > 0.05f)
        {
            g.setColour (pink.withAlpha (0.65f));
            g.drawLine (originX, rulerY, t.pos.x, rulerY, 2.0f);
            g.fillEllipse (originX - 3.0f, rulerY - 3.0f, 6.0f, 6.0f);
            const int dir = bendSemis > 0.0f ? 1 : -1;
            const int nTicks = juce::jmin (cfg.cols, (int) std::floor (std::abs (bendSemis)));
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            for (int i = 1; i <= nTicks; ++i)
            {
                const float tx = originX + (float) (dir * i) * cw;
                g.drawLine (tx, rulerY - 4.0f, tx, rulerY + 4.0f, 1.0f);
            }
        }

        // live pitch readout: note name + cents offset
        const float absPitch = (float) rowBaseNote (t.row) + t.pitchNow;
        const int note = juce::jlimit (0, 127, (int) std::lround (absPitch));
        const int cents = (int) std::lround ((absPitch - (float) note) * 100.0f);
        juce::String label = juce::MidiMessage::getMidiNoteName (note, true, true, 4);
        if (std::abs (cents) >= 3) label << (cents > 0 ? " +" : " ") << cents << "c";
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (label, juce::Rectangle<float> (t.pos.x - 45.0f, t.pos.y - 46.0f, 90.0f, 16.0f), juce::Justification::centred);

        if (vt->v.vibEnv > 0.02f)
        {
            g.setColour (violet.withAlpha (0.8f * vt->v.vibEnv));
            g.drawEllipse (t.pos.x - 24.0f - 4.0f * vt->v.vibEnv, t.pos.y - 24.0f - 4.0f * vt->v.vibEnv,
                           48.0f + 8.0f * vt->v.vibEnv, 48.0f + 8.0f * vt->v.vibEnv, 1.0f);
        }
    }
    g.restoreState();

    g.setColour (juce::Colour (0x22ffffff));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 16.0f, 1.0f);
}

// ----------------------------------------------------------------- mapping
float GridSurface::snappedPitchAt (float x, int row) const
{
    // continuous cell coordinate; cell centre k <-> pitch k
    const float p = x / cellW() - 0.5f;
    const float k = std::floor (p);
    const float frac = p - k;                      // 0..1 between cell k and k+1
    const float d = juce::jlimit (0.0f, 0.499f, cfg.snapDeadzone);
    float f;
    if (frac < d)              f = 0.0f;
    else if (frac > 1.0f - d)  f = 1.0f;
    else                       f = (frac - d) / (1.0f - 2.0f * d);
    return applyScalePull (k + f, row);
}

float GridSurface::pressureFor (const juce::MouseEvent& e, int row) const
{
    if (e.isPressureValid()) return e.pressure;
    if (! cfg.yIsPressure)   return 0.5f;
    // within the row cell: bottom edge = 0.1, top edge = 1.0
    const float ch = cellH();
    const float rowTop = (float) getHeight() - (float) (row + 1) * ch;
    const float yin = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - rowTop) / ch);
    return 0.1f + 0.9f * yin;
}

// ----------------------------------------------------------------- channels
int GridSurface::allocateChannel()
{
    for (int ch = 2; ch <= 16; ++ch)
        if (! channelBusy[(size_t) ch]) { channelBusy[(size_t) ch] = true; return ch; }
    return 0;  // all 15 member channels busy
}
void GridSurface::releaseChannel (int ch) { if (ch >= 2 && ch <= 16) channelBusy[(size_t) ch] = false; }

// ----------------------------------------------------------------- MPE out
void GridSurface::sendBend (Voice& v, float semis)
{
    if (v.channel < 2) return;
    if (std::abs (semis - v.lastBend) < 1.0e-4f) return;
    v.lastBend = semis;
    const float norm = juce::jlimit (-1.0f, 1.0f, semis / (float) cfg.bendRangeSemis);
    const int v14 = juce::jlimit (0, 16383, (int) std::lround (8192.0f + norm * 8191.0f));
    post (juce::MidiMessage::pitchWheel (v.channel, v14));
}
void GridSurface::sendPressure (Voice& v, float p)
{
    if (v.channel < 2) return;
    p = juce::jlimit (0.0f, 1.0f, p);
    if (std::abs (p - v.lastPressure) < 1.0f / 127.0f) return;
    v.lastPressure = p;
    post (juce::MidiMessage::channelPressureChange (v.channel, (int) std::lround (p * 127.0f)));
}
void GridSurface::sendTimbre (Voice& v, float p)
{
    if (v.channel < 2) return;
    p = juce::jlimit (0.0f, 1.0f, p);
    if (std::abs (p - v.lastTimbre) < 1.0f / 127.0f) return;
    v.lastTimbre = p;
    post (juce::MidiMessage::controllerEvent (v.channel, 74, (int) std::lround (p * 127.0f)));
}
void GridSurface::applyBend (Voice& v)
{
    const float vib = cfg.autoVibrato * kVibMaxSemis * v.vibEnv * std::sin (v.vibPhase);
    sendBend (v, v.baseBend + vib);
}
void GridSurface::applyPressure (Voice& v)
{
    sendPressure (v, v.basePressure + v.accent * 0.5f);
}

// ----------------------------------------------------------------- hammer helpers
GridSurface::Touch* GridSurface::voiceTouch (const Touch& t)
{
    if (! t.inStack) return const_cast<Touch*> (&t);
    if (t.row < 0 || t.row >= kMaxRows || rowStack[(size_t) t.row].empty()) return nullptr;
    auto it = touches.find (rowStack[(size_t) t.row].front());
    return it != touches.end() ? &it->second : nullptr;
}
const GridSurface::Touch* GridSurface::voiceTouch (const Touch& t) const
{
    return const_cast<GridSurface*> (this)->voiceTouch (t);
}

bool GridSurface::isDriver (int id, const Touch& t) const
{
    if (! t.inStack) return true;
    const auto& st = rowStack[(size_t) t.row];
    return ! st.empty() && st.back() == id;
}

// Point the row's voice at the pitch under the current top finger.
void GridSurface::retarget (int row, float accent)
{
    auto& st = rowStack[(size_t) row];
    if (st.empty()) return;
    auto vit = touches.find (st.front());
    auto dit = touches.find (st.back());
    if (vit == touches.end() || dit == touches.end() || ! vit->second.hasVoice) return;
    Voice& v = vit->second.v;
    v.baseBend = dit->second.pitchNow - (float) (v.noteNumber - rowBaseNote (row));
    v.anchorPitch = dit->second.pos.x / cellW() - 0.5f;
    v.anchorMs = nowMs();
    applyBend (v);
    v.accent = juce::jmax (v.accent, accent);
    applyPressure (v);
}

void GridSurface::endVoice (Touch& t)
{
    if (t.hasVoice && t.v.channel >= 2)
    {
        post (juce::MidiMessage::noteOff (t.v.channel, t.v.noteNumber, 0.5f));
        releaseChannel (t.v.channel);
        t.v.channel = 0;
    }
    t.hasVoice = false;
    if (t.row >= 0 && t.row < (int) vibAmp.size()) vibAmp[(size_t) t.row] = 1.0f;  // visual ring-out
}

// ----------------------------------------------------------------- touches
void GridSurface::mouseDown (const juce::MouseEvent& e)
{
    const int id = e.source.getIndex();
    if (touches.count (id) != 0) releaseTouch (id);   // missed mouseUp: never leave a stuck note

    if (getWidth() <= 0 || getHeight() <= 0) return;
    const int row = rowAt (e.position.y);
    const float pitch = snappedPitchAt (e.position.x, row);

    // hammer-on: row already held -> legato re-pitch of its voice, no new note
    if (cfg.hammerOn && ! rowStack[(size_t) row].empty())
    {
        Touch t;
        t.row = row;
        t.inStack = true;
        t.pos = e.position;
        t.pitchNow = pitch;
        touches[id] = t;
        rowStack[(size_t) row].push_back (id);
        // pressure/timbre follow the new finger as well
        if (auto vit = touches.find (rowStack[(size_t) row].front()); vit != touches.end())
        {
            vit->second.v.basePressure = pressureFor (e, row);
            const float chh = cellH();
            const float rowTop = (float) getHeight() - (float) (row + 1) * chh;
            sendTimbre (vit->second.v, juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - rowTop) / chh));
        }
        retarget (row, 0.45f);   // hammer accent
        repaint();
        return;
    }

    const int ch = allocateChannel();
    if (ch == 0) return;

    Touch t;
    t.hasVoice = true;
    t.row = row;
    t.pos = e.position;
    t.pitchNow = pitch;
    Voice& v = t.v;
    v.channel = ch;
    const int cell = juce::jlimit (0, cfg.cols - 1, (int) std::lround (pitch));
    v.noteNumber = juce::jlimit (0, 127, rowBaseNote (row) + cell);

    const float pressure = pressureFor (e, row);
    const float velocity = e.isPressureValid() ? juce::jmax (0.2f, e.pressure) : pressure;
    v.basePressure = pressure;
    v.baseBend = pitch - (float) (v.noteNumber - rowBaseNote (row));
    v.anchorPitch = e.position.x / cellW() - 0.5f;
    v.anchorMs = nowMs();

    // MPE order: set per-note expression *before* note-on on that channel
    post (juce::MidiMessage::pitchWheel (ch, 8192));
    v.lastBend = 0.0f;
    sendBend (v, v.baseBend);
    sendPressure (v, pressure);
    sendTimbre (v, 0.5f);
    post (juce::MidiMessage::noteOn (ch, v.noteNumber, velocity));

    if (cfg.hammerOn)
    {
        t.inStack = true;
        rowStack[(size_t) row].push_back (id);
    }
    touches[id] = t;
    vibAmp[(size_t) row] = 0.0f;
    repaint();
}

void GridSurface::mouseDrag (const juce::MouseEvent& e)
{
    auto it = touches.find (e.source.getIndex());
    if (it == touches.end()) return;
    Touch& t = it->second;
    t.pos = e.position;
    t.trail.push_back (e.position);
    if (t.trail.size() > 28) t.trail.erase (t.trail.begin());

    // X → pitch relative to the voice's own cell (stays on its row: sliding
    // vertically off the string mutes nothing, like a real string bend)
    t.pitchNow = snappedPitchAt (e.position.x, t.row);

    if (isDriver (it->first, t))
    {
        if (Touch* vt = voiceTouch (t); vt != nullptr && vt->hasVoice)
        {
            Voice& v = vt->v;
            v.baseBend = t.pitchNow - (float) (v.noteNumber - rowBaseNote (t.row));

            // vibrato is cancelled as soon as the finger drifts from where it rested
            const float raw = e.position.x / cellW() - 0.5f;
            if (std::abs (raw - v.anchorPitch) > kVibMoveThreshold)
            {
                v.anchorPitch = raw;
                v.anchorMs = nowMs();
            }
            applyBend (v);

            // Y → pressure (or force sensor) and timbre
            v.basePressure = pressureFor (e, t.row);
            applyPressure (v);
            const float ch = cellH();
            const float rowTop = (float) getHeight() - (float) (t.row + 1) * ch;
            sendTimbre (v, juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - rowTop) / ch));
        }
    }
    repaint();
}

void GridSurface::releaseTouch (int id)
{
    auto it = touches.find (id);
    if (it == touches.end()) return;
    Touch t = it->second;   // copy: the map entry goes away below
    touches.erase (it);

    if (! t.inStack)
    {
        endVoice (t);
        return;
    }

    auto& st = rowStack[(size_t) t.row];
    const bool wasOwner  = ! st.empty() && st.front() == id;
    const bool wasDriver = ! st.empty() && st.back() == id;
    st.erase (std::remove (st.begin(), st.end(), id), st.end());

    if (st.empty())
    {
        endVoice (t);          // last finger on the row: note off
        return;
    }

    if (wasOwner)
    {
        // hand the sounding voice to the next finger so nothing sticks or cuts out
        auto nit = touches.find (st.front());
        if (nit != touches.end())
        {
            nit->second.v = t.v;
            nit->second.hasVoice = true;
        }
    }
    if (wasDriver) retarget (t.row, 0.2f);   // pull-off: bend back to the finger underneath
}

void GridSurface::keyNote (int note, bool down)
{
    note = juce::jlimit (0, 127, note);
    auto it = keyChannels.find (note);
    if (down)
    {
        if (it != keyChannels.end()) return;               // key auto-repeat
        const int ch = allocateChannel();
        if (ch == 0) return;
        keyChannels[note] = ch;
        // MPE order: expression first, then note-on
        post (juce::MidiMessage::pitchWheel (ch, 8192));
        post (juce::MidiMessage::channelPressureChange (ch, 100));
        post (juce::MidiMessage::controllerEvent (ch, 74, 64));
        post (juce::MidiMessage::noteOn (ch, note, 0.7f));
    }
    else if (it != keyChannels.end())
    {
        post (juce::MidiMessage::noteOff (it->second, note, 0.5f));
        releaseChannel (it->second);
        keyChannels.erase (it);
    }
    repaint();
}

void GridSurface::releaseAllKeys()
{
    for (auto& [note, ch] : keyChannels)
    {
        post (juce::MidiMessage::noteOff (ch, note, 0.5f));
        releaseChannel (ch);
    }
    keyChannels.clear();
    repaint();
}

void GridSurface::releaseAllTouches()
{
    releaseAllKeys();
    std::vector<int> ids;
    for (auto& [id, t] : touches) ids.push_back (id);
    for (int id : ids) releaseTouch (id);
    for (auto& st : rowStack) st.clear();
    channelBusy.fill (false);
    repaint();
}

void GridSurface::mouseUp (const juce::MouseEvent& e)
{
    releaseTouch (e.source.getIndex());
    repaint();
}

void GridSurface::mouseExit (const juce::MouseEvent& e)
{
    // a captured drag keeps its touch; only release if the pointer left without being held
    if (! e.source.isDragging())
        releaseTouch (e.source.getIndex());
}

void GridSurface::visibilityChanged()      { if (! isShowing()) releaseAllTouches(); }
void GridSurface::parentHierarchyChanged() { if (! isShowing()) releaseAllTouches(); }
