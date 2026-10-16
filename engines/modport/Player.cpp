// ProTracker 2.3D replayer, after pt2-clone's pt2_replayer.c / pt2_paula.c
// (Copyright (c) 2010-2026 Olav Sørensen, BSD 3-Clause; see NOTICE.md),
// widened to 8 channels and five octaves.
#include "Player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace modport
{

namespace
{
const std::uint8_t kVibratoTable[32] = {
    0x00, 0x18, 0x31, 0x4A, 0x61, 0x78, 0x8D, 0xA1, 0xB4, 0xC5, 0xD4, 0xE0, 0xEB, 0xF4, 0xFA, 0xFD,
    0xFF, 0xFD, 0xFA, 0xF4, 0xEB, 0xE0, 0xD4, 0xC5, 0xB4, 0xA1, 0x8D, 0x78, 0x61, 0x4A, 0x31, 0x18
};
const std::uint8_t kFunkTable[16] = { 0x00, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0D, 0x10, 0x13, 0x16, 0x1A, 0x20, 0x2B, 0x40, 0x80 };

// minBLEP, zero crossings 16, oversampling 16 (aciddose, from pt2-clone)
const float kMinBlep[257] = {
     1.0000477302613517416f, 1.0000703265259194286f, 1.0000262954869634235f, 0.9999104247733368034f,
     0.9997157443790558595f, 0.9994330149197339086f, 0.9990500857713285887f, 0.9985511219195251087f,
     0.9979157062335919370f, 0.9971178326926340985f, 0.9961248154952055955f, 0.9948961485703640140f,
     0.9933823593234317739f, 0.9915239090030570912f, 0.9892501993644792213f, 0.9864787508331824828f,
     0.9831146206825892575f, 0.9790501304255075921f, 0.9741649693586746928f, 0.9683267357717054713f,
     0.9613919686347883742f, 0.9532077106463556770f, 0.9436136285285890990f, 0.9324446987272798637f,
     0.9195344466691151020f, 0.9047187060538732783f, 0.8878398420296869098f, 0.8687513593312519156f,
     0.8473227944375107956f, 0.8234447704476933749f, 0.7970340756049164588f, 0.7680386121007229949f,
     0.7364420517831927748f, 0.7022680303646210431f, 0.6655837122341694556f, 0.6265025644004150740f,
     0.5851861905894381044f, 0.5418450950558918455f, 0.4967382699459244044f, 0.4501715295677637396f,
     0.4024945489393364495f, 0.3540966015460170202f, 0.3054010312278475636f, 0.2568585342499346558f,
     0.2089393685130542522f, 0.1621246460974391512f, 0.1168969014596899919f, 0.0737301592279361734f,
     0.0330797513739862215f,-0.0046278475512338936f,-0.0390048873823494666f,-0.0697116292604941790f,
    -0.0964647767093624875f,-0.1190447905608251339f,-0.1373018517595622767f,-0.1511602687179081639f,
    -0.1606211659994899177f,-0.1657633375556412103f,-0.1667421991415036220f,-0.1637868293095470773f,
    -0.1571951447710946692f,-0.1473273120888395071f,-0.1345975517409976063f,-0.1194645407415074190f,
    -0.1024206644738059890f,-0.0839804056280038791f,-0.0646681867786920578f,-0.0450060021366877130f,
    -0.0255011826063778063f,-0.0066346360852734603f, 0.0111501080726254947f, 0.0274567449958385452f,
     0.0419446584514933316f, 0.0543357729883130469f, 0.0644196131370170927f, 0.0720564437641972172f,
     0.0771784246024087850f, 0.0797887728449645922f, 0.0799589884682101459f, 0.0778242556005649261f,
     0.0735771878467293278f, 0.0674601341940760518f, 0.0597563033841086166f, 0.0507799970999210509f,
     0.0408662649895026181f, 0.0303603067514581562f, 0.0196069479612341578f, 0.0089405070970495753f,
    -0.0013246482081264665f,-0.0109025865007772501f,-0.0195431073564850052f,-0.0270376576677117432f,
    -0.0332237305394043891f,-0.0379876606806542061f,-0.0412657844962587075f,-0.0430439870972044586f,
    -0.0433557103124065854f,-0.0422785437568130862f,-0.0399295635284086167f,-0.0364596188316990630f,
    -0.0320467946929081995f,-0.0268892981827763312f,-0.0211980257636115339f,-0.0151890704304555764f,
    -0.0090764194553641132f,-0.0030650773168921556f, 0.0026551753615487940f, 0.0079152062478091562f,
     0.0125709938660189587f, 0.0165070221469508817f, 0.0196385421289703745f, 0.0219126779344609758f,
     0.0233083952284523256f, 0.0238353891250968619f, 0.0235319836335969659f, 0.0224621650980049159f,
     0.0207118967713227006f, 0.0183848800034730822f, 0.0155979391055239645f, 0.0124762116364726186f,
     0.0091483237641666864f, 0.0057417218473517556f, 0.0023783170654907890f,-0.0008294194250053805f,
    -0.0037817967606831484f,-0.0063945924755681527f,-0.0086010292023157020f,-0.0103530098334944001f,
    -0.0116216097291982345f,-0.0123968518397700699f,-0.0126868154975107086f,-0.0125161512979873567f,
    -0.0119240922816280862f,-0.0109620650623401504f,-0.0096910133459421205f,-0.0081785503450736048f,
    -0.0064960560250743584f,-0.0047158301788018369f,-0.0029084034555543611f,-0.0011400962200064214f,
     0.0005290998458667121f, 0.0020472594272570623f, 0.0033718407258998130f, 0.0044705608305281395f,
     0.0053218263185221635f, 0.0059147333317129914f, 0.0062486669637696907f, 0.0063325432361187482f,
     0.0061837478235316776f, 0.0058268337450093155f, 0.0052920453161976388f, 0.0046137377508039690f,
     0.0038287610068399431f, 0.0029748730160543677f, 0.0020892416238459640f, 0.0012070867911180888f,
     0.0003605053137761188f,-0.0004224900212018401f,-0.0011186958109406235f,-0.0017101978657877311f,
    -0.0021846059849888523f,-0.0025350539493808797f,-0.0027599836404749540f,-0.0028627394193863481f,
    -0.0028510045105526855f,-0.0027361150176634062f,-0.0025322893172763906f,-0.0022558109708829808f,
    -0.0019242020804577712f,-0.0015554213581010150f,-0.0011671173084782766f,-0.0007759620906248776f,
    -0.0003970861128210880f,-0.0000436275087427707f, 0.0002735953716143057f, 0.0005462861799454866f,
     0.0007687506805363838f, 0.0009378627976334288f, 0.0010529287404153116f, 0.0011154645668973284f,
     0.0011289048235580167f, 0.0010982612080436753f, 0.0010297505723515206f, 0.0009304110777853725f,
     0.0008077240290279897f, 0.0006692569780186192f, 0.0005223412430787977f, 0.0003737941898757299f,
     0.0002296936262087236f, 0.0000952086254601105f,-0.0000255118442794698f,-0.0001293938220771447f,
    -0.0002144405097769575f,-0.0002796873836864508f,-0.0003251174847873964f,-0.0003515451441525616f,
    -0.0003604769473845936f,-0.0003539588239428851f,-0.0003344178062763300f,-0.0003045062980902643f,
    -0.0002669556911830751f,-0.0002244449539125465f,-0.0001794884622946999f,-0.0001343459365493336f,
    -0.0000909559560488899f,-0.0000508932202670833f,-0.0000153485577887860f, 0.0000148702975205883f,
     0.0000393199152742675f, 0.0000578876582698600f, 0.0000707470635261192f, 0.0000783058195432098f,
     0.0000811499393448619f, 0.0000799874519480083f, 0.0000755945206110041f, 0.0000687663822549972f,
     0.0000602749277155524f, 0.0000508341447950342f, 0.0000410740603062377f, 0.0000315232737098524f,
     0.0000225996980950096f, 0.0000146087321789519f, 0.0000077477909467512f, 0.0000021159270461592f,
    -0.0000022728216146247f,-0.0000054737276186147f,-0.0000075946076493092f,-0.0000087791482829482f,
    -0.0000091912899144718f,-0.0000090014159571477f,-0.0000083748626165846f,-0.0000074630357144956f,
    -0.0000063972090792165f,-0.0000052848949776790f,-0.0000042085288174683f,-0.0000032261024680931f,
    -0.0000023733143901201f,-0.0000016667787374929f,-0.0000011078456395590f,-0.0000006866249747596f,
    -0.0000003858688186494f,-0.0000001844454404328f,-0.0000000602222727236f, 0.0000000077407240470f,
     0.0000000377088328857f, 0.0000000444579428691f, 0.0000000390342963101f, 0.0000000289629328710f,
     0.0000000187639946982f, 0.0000000106369370086f, 0.0000000051870995047f, 0.0000000020936704675f,
     0.0000000006489518121f, 0.0000000001320180639f, 0.0000000000115913357f, 0.0000000000000000000f,
     0.0000000000000000000f
};

constexpr std::uint32_t kNullSize = 0x20000;   // silent memory for NULL pointers
constexpr double kPi = 3.14159265358979323846;

// left / right per channel: Amiga LRRL, repeated for 5..8 channels
constexpr bool kLeft[kMaxChannels] = { true, false, false, true, true, false, false, true };
} // namespace

double ciaBpmToHz (int bpm)
{
    if (bpm <= 0) return 50.0;
    const std::uint32_t ciaPeriod = 1773447u / (std::uint32_t) bpm;   // PT truncates here
    return (kPaulaClock / 5.0) / (double) (ciaPeriod + 1);
}

//==============================================================================
void Blep::add (float offset, float amplitude)
{
    float f = offset * 16.0f;
    const int fi = std::min (15, std::max (0, (int) f));
    const float* src = kMinBlep + fi;
    f -= (float) fi;
    int i = index;
    for (int n = 0; n < kNS; ++n)
    {
        buffer[i] += amplitude * (src[0] + (src[1] - src[0]) * f);
        src += 16;
        i = (i + 1) & kRNS;
    }
    samplesLeft = kNS;
}

float Blep::run (float input)
{
    const float out = input + buffer[index];
    buffer[index] = 0.0f;
    index = (index + 1) & kRNS;
    --samplesLeft;
    return out;
}

void OnePole::setup (double rate, double fc)
{
    if (fc >= rate / 2.0) fc = rate / 2.0 - 1e-4;
    const double b = std::exp ((-2.0 * kPi) * fc / rate);
    b1 = (float) b;
    a0 = (float) (1.0 - b);
}

void TwoPole::setup (double rate, double fc, double q)
{
    if (fc >= rate / 2.0) fc = rate / 2.0 - 1e-4;
    const double a = 1.0 / std::tan ((kPi * fc) / rate);
    const double r = 1.0 / q;
    const double x1 = 1.0 / (1.0 + r * a + a * a);
    a1 = (float) x1;
    a2 = (float) (2.0 * x1);
    b1 = (float) (2.0 * (1.0 - a * a) * x1);
    b2 = (float) ((1.0 - r * a + a * a) * x1);
}

float TwoPole::lp (float x)
{
    const float y = x * a1 + z[0] * a2 + z[1] * a1 - z[2] * b1 - z[3] * b2;
    z[1] = z[0]; z[0] = x; z[3] = z[2]; z[2] = y;
    return y;
}

void AmigaFilter::setup (double rate, Model m)
{
    clear();
    useLo = m == Model::A500;
    useHi = m != Model::Off;
    if (m == Model::A500)
    {
        lo.setup (rate, 1.0 / (2.0 * kPi * 360.0 * 1e-7));          // ~4421 Hz
        hi.setup (rate, 1.0 / (2.0 * kPi * 1390.0 * 2.233e-5));     // ~5.1 Hz
    }
    else
    {
        hi.setup (rate, 1.0 / (2.0 * kPi * 1360.0 * 2.2e-5));       // ~5.3 Hz
    }
    const double R1 = 10000.0, R2 = 10000.0, C1 = 6.8e-9, C2 = 3.9e-9;
    led.setup (rate, 1.0 / (2.0 * kPi * std::sqrt (R1 * R2 * C1 * C2)), std::sqrt (R1 * R2 * C1 * C2) / (C2 * (R1 + R2)));
}

float AmigaFilter::process (float x, bool ledOn)
{
    if (useLo) x = lo.lp (x);
    if (ledOn) x = led.lp (x);
    if (useHi) x = hi.hp (x);
    return x;
}

//==============================================================================
Player::Player()
{
    visited.assign (128, {});
}

void Player::setModule (const Module* m)
{
    mod = m;
    mem.clear();
    if (mod == nullptr) return;

    numCh = mod->channels;
    extended = mod->extendedPeriods;

    // memory: silence, then each sample (a loop past the end extends the slot with
    // silence, as pt2-clone), non-looping samples start with two zero bytes so
    // Paula's one-word "loop" is silent.
    mem.assign (kNullSize, 0);
    for (int s = 0; s < kMaxSamples; ++s)
    {
        const auto& smp = mod->samples[(std::size_t) s];
        sampleSkip[(std::size_t) s] = 0;
        if (s >= mod->numSamples || smp.lengthWords == 0)
        {
            sampleBase[(std::size_t) s] = -1;
            continue;
        }
        sampleBase[(std::size_t) s] = (std::int64_t) mem.size();
        std::uint32_t slot = smp.lengthBytes();
        if (smp.looped()) slot = std::max (slot, ((std::uint32_t) smp.loopStartWords + smp.loopLengthWords) * 2u);
        const auto start = mem.size();
        mem.resize (start + slot + 2, 0);
        std::memcpy (mem.data() + start, smp.data.data(), std::min<std::size_t> (smp.data.size(), slot));
        if (mod->layout == Layout::SoundTracker15 && smp.looped())
        {
            // SoundTracker: loop start in bytes, only the looped part ever plays
            sampleSkip[(std::size_t) s] = std::min<std::uint32_t> (smp.loopStartWords, smp.lengthBytes());
        }
        else if (smp.loopStartWords == 0 && smp.loopLengthWords <= 1 && smp.lengthBytes() >= 2)
        {
            mem[start] = 0;
            mem[start + 1] = 0;
        }
    }
    mem.resize (mem.size() + kNullSize, 0);
    memPristine = mem;

    // five-octave tables for non-ProTracker files: C-0..B-4, then 0
    for (int f = 0; f < 16; ++f)
    {
        for (int n = 0; n < 60; ++n) extTable[(std::size_t) (f * 61 + n)] = extendedPeriod (f, n);
        extTable[(std::size_t) (f * 61 + 60)] = 0;
    }

    vblank = mod->timing == Timing::VBlank;
    refTempo = 125;
    if (mod->ultimate && mod->restartByte != 0x78 && mod->restartByte < 240)
        refTempo = std::max (32, std::min (255, (709379 * 125 / 50) / ((240 - mod->restartByte) * 122)));   // as OpenMPT
    else if (mod->layout != Layout::SoundTracker15)
    {
        const int p = mod->patternAt (0);
        for (int c = 0; c < numCh; ++c)
        {
            const auto& cl = mod->cell (p, 0, c);
            if (cl.effect == 0xF && cl.param >= 0x20) refTempo = cl.param;
        }
    }
    restart();
}

void Player::prepare (double sampleRate)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    periodToDeltaDiv = (float) (kPaulaClock / sr);
    setModel (model);
    scratch.assign (4096, 0.0f);
}

void Player::setModel (Model m)
{
    model = m;
    for (auto& f : mixFilter) f.setup (sr, m);
    for (auto& f : stemFilter) f.setup (sr, m);
}

void Player::restart (int order, int startRow)
{
    for (int i = 0; i < kMaxChannels; ++i)
    {
        ch[(std::size_t) i] = Chan {};
        ch[(std::size_t) i].index = i;
        voices[(std::size_t) i] = Voice {};
    }
    if (! memPristine.empty()) mem = memPristine;
    for (auto& f : mixFilter) f.clear();
    for (auto& f : stemFilter) f.clear();
    for (auto& v : visited) v.reset();

    speed = 6;
    bpm = mod != nullptr && mod->ultimate ? refTempo : 125;
    ciaSetBPM = -1;
    pattDelTime = pattDelTime2 = 0;
    lowMask = 0xFF;
    pBreakPosition = 0;
    posJumpAssert = pBreakFlag = stopSong = ended = wrapPending = false;
    ledOn = false;
    songSeconds = songBeats = 0.0;
    ticksDone = 0;
    tickRemain = 0.0;

    const int len = mod != nullptr ? mod->songLength : 1;
    gLoopRow = gLoopCount = 0;
    rowLoopJumped = rowLoopSeen = false;
    modPos = (order >= 0 && order < len) ? order : 0;
    if (mod != nullptr) modPos = resolveOrder (modPos);
    modPattern = mod != nullptr ? mod->orders[(std::size_t) modPos] : 0;
    row = startRow & 63;
    tickCount = speed - 1;
    curOrder = modPos;
    curRow = row;
    curPattern = modPattern;
}

//==============================================================================
Cell Player::fetch (int pattern, int r, int c) const
{
    Cell cl = mod->cell (pattern, r, c);
    if (mod->layout == Layout::SoundTracker15)
    {
        if (mod->ultimate)
        {
            // UST: 1xy = arpeggio, 2xy = pitch bend (x up, y down)
            if (cl.effect == 1) cl.effect = 0;
            else if (cl.effect == 2 && (cl.param & 0xF0) == 0) cl.effect = 1;
            else if (cl.effect == 2 && (cl.param & 0x0F) == 0) cl.param >>= 4;
            else if (cl.effect != 0) { cl.effect = 0; cl.param = 0; }
        }
        else if (cl.effect == 0xF)
        {
            // SoundTracker 2.x: Fxx is always speed
            cl.param = (std::uint8_t) std::min<int> (cl.param, 0x1F);
        }
    }
    return cl;
}

const std::int16_t* Player::periods (int finetune) const
{
    return extended ? &extTable[(std::size_t) ((finetune & 15) * 61)] : &kPeriodTable[(finetune & 15) * 37];
}

void Player::paulaPeriod (int c, int period)
{
    auto& v = voices[(std::size_t) c];
    int real = period & 0xFFFF;
    if (real == 0) real = 65536;
    else if (real < (extended ? 14 : 113)) real = extended ? 14 : 113;
    v.storedDelta = periodToDeltaDiv / (float) real;
    v.period = period & 0xFFFF;
    if (v.blepDelta == 0.0f) v.blepDelta = v.delta;
}

void Player::paulaVolume (int c, int vol)
{
    int real = vol & 127;
    if (real > 64) real = 64;
    voices[(std::size_t) c].storedVol = (float) real * (1.0f / (128.0f * 64.0f));
    voices[(std::size_t) c].volume = real;
}

void Player::dmaOn (int c)
{
    auto& v = voices[(std::size_t) c];
    v.loc = v.storedLoc;
    v.lenCounter = v.storedLen;
    v.sampleCounter = 0;
    v.justStarted = true;
    v.finished = false;
    v.blepPhase = v.phase;
    v.blepDelta = v.delta;
    v.delta = v.storedDelta;
    v.nextStage = true;
    v.phase = 0.0f;
    v.active = true;
}

//==============================================================================
// replayer
void Player::updateFunk (Chan& c)
{
    const int funkSpeed = c.n_glissfunk >> 4;
    if (funkSpeed == 0) return;
    c.n_funkoffset = (std::uint8_t) (c.n_funkoffset + kFunkTable[funkSpeed]);
    if (c.n_funkoffset >= 128)
    {
        c.n_funkoffset = 0;
        if (c.n_loopstart >= 0 && c.n_wavestart >= 0)
        {
            if (++c.n_wavestart >= c.n_loopstart + ((std::int64_t) c.n_replen << 1)) c.n_wavestart = c.n_loopstart;
            if ((std::size_t) c.n_wavestart < mem.size()) mem[(std::size_t) c.n_wavestart] = (std::int8_t) (-1 - mem[(std::size_t) c.n_wavestart]);
        }
    }
}

void Player::jumpLoop (Chan& c)
{
    if (tickCount != 0) return;
    if (mod->flow != Flow::ProTracker)
    {
        // one loop for all channels (libxmp: FLOW_MODE_OCTALYSER / FLOW_MODE_DTM_2015)
        const int x = c.n_cmd & 0xF;
        if (mod->flow == Flow::DigitalTracker && rowLoopSeen) return;   // first E6x of the row only
        rowLoopSeen = true;
        if (x == 0)
        {
            if (mod->flow == Flow::Octalyser && gLoopCount >= 1) return;
            gLoopRow = row;
            return;
        }
        if (gLoopCount > 0)
        {
            if (--gLoopCount > 0) { pBreakPosition = (std::int8_t) gLoopRow; pBreakFlag = true; rowLoopJumped = true; }
            else if (mod->flow == Flow::Octalyser) { rowLoopJumped = false; pBreakFlag = false; }
        }
        else
        {
            gLoopCount = x;
            pBreakPosition = (std::int8_t) gLoopRow;
            pBreakFlag = true;
            rowLoopJumped = true;
        }
        if (rowLoopJumped)
            for (int r = std::max (0, gLoopRow); r <= row && r < 64; ++r) visited[(std::size_t) modPos].reset ((std::size_t) r);
        return;
    }
    if ((c.n_cmd & 0xF) == 0)
    {
        c.n_pattpos = (std::int8_t) row;
    }
    else
    {
        if (c.n_loopcount == 0) c.n_loopcount = (std::int8_t) (c.n_cmd & 0xF);
        else if (--c.n_loopcount == 0) return;
        pBreakPosition = c.n_pattpos;
        pBreakFlag = true;
        for (int r = std::max (0, (int) pBreakPosition); r <= row && r < 64; ++r)
            visited[(std::size_t) modPos].reset ((std::size_t) r);
    }
}

void Player::doRetrg (Chan& c)
{
    const int i = c.index;
    dmaOff (i);
    paulaData (i, c.n_start);
    paulaLength (i, c.n_length);
    paulaPeriod (i, c.n_period);
    dmaOn (i);
    paulaData (i, c.n_loopstart);
    paulaLength (i, c.n_replen);
    if (onTrigger)
    {
        Trigger t;
        t.channel = i; t.sample = c.n_samplenum + 1; t.period = c.n_period; t.notePeriod = c.n_note & 0xFFF;
        t.volume = c.n_volume; t.order = curOrder; t.row = curRow; t.offsetBytes = c.offsetApplied;
        onTrigger (t);
    }
}

void Player::volumeSlide (Chan& c)
{
    const std::uint8_t param = (std::uint8_t) (c.n_cmd & 0xFF);
    if ((param & 0xF0) == 0)
    {
        c.n_volume = (std::int8_t) (c.n_volume - (param & 0x0F));
        if (c.n_volume < 0) c.n_volume = 0;
    }
    else
    {
        c.n_volume = (std::int8_t) (c.n_volume + (param >> 4));
        if (c.n_volume > 64) c.n_volume = 64;
    }
}

void Player::arpeggio (Chan& c)
{
    const int arpTick = tickCount % 3;
    int arpNote;
    if (arpTick == 1) arpNote = (c.n_cmd >> 4) & 0xF;
    else if (arpTick == 2) arpNote = c.n_cmd & 0xF;
    else
    {
        paulaPeriod (c.index, c.n_period);
        return;
    }
    if (extended)
    {
        const auto* p = periods (c.n_finetune);
        for (int base = 0; base < 60; ++base)
            if (c.n_period >= p[base])
            {
                paulaPeriod (c.index, p[std::min (59, base + arpNote)]);
                return;
            }
        return;
    }
    const auto* p = &kPeriodTable[c.n_finetune * 37];
    for (int base = 0; base < 37; ++base)
        if (c.n_period >= p[base])
        {
            paulaPeriod (c.index, p[base + arpNote]);   // the padded table makes overflow safe
            return;
        }
}

void Player::portaUp (Chan& c)
{
    c.n_period = (std::int16_t) (c.n_period - ((c.n_cmd & 0xFF) & lowMask));
    lowMask = 0xFF;
    const int lo = extended ? 28 : 113;
    if ((c.n_period & 0xFFF) < lo) c.n_period = (std::int16_t) ((c.n_period & 0xF000) | lo);
    paulaPeriod (c.index, c.n_period & 0xFFF);
}

void Player::portaDown (Chan& c)
{
    c.n_period = (std::int16_t) (c.n_period + ((c.n_cmd & 0xFF) & lowMask));
    lowMask = 0xFF;
    const int hi = extended ? 3424 : 856;
    if ((c.n_period & 0xFFF) > hi) c.n_period = (std::int16_t) ((c.n_period & 0xF000) | hi);
    paulaPeriod (c.index, c.n_period & 0xFFF);
}

void Player::setTonePorta (Chan& c)
{
    const int note = c.n_note & 0xFFF;
    const auto* p = periods (c.n_finetune);
    const int n = numNotes();
    int i = 0;
    while (true)
    {
        if (note >= p[i]) break;
        if (++i >= n + 1) { i = n - 1; break; }
    }
    if ((c.n_finetune & 8) && i > 0) --i;
    c.n_wantedperiod = p[i];
    c.n_toneportdirec = 0;
    if (c.n_period == c.n_wantedperiod) c.n_wantedperiod = 0;
    else if (c.n_period > c.n_wantedperiod) c.n_toneportdirec = 1;
}

void Player::tonePortNoChange (Chan& c)
{
    if (c.n_wantedperiod <= 0) return;
    if (c.n_toneportdirec > 0)
    {
        c.n_period = (std::int16_t) (c.n_period - c.n_toneportspeed);
        if (c.n_period <= c.n_wantedperiod) { c.n_period = c.n_wantedperiod; c.n_wantedperiod = 0; }
    }
    else
    {
        c.n_period = (std::int16_t) (c.n_period + c.n_toneportspeed);
        if (c.n_period >= c.n_wantedperiod) { c.n_period = c.n_wantedperiod; c.n_wantedperiod = 0; }
    }

    if ((c.n_glissfunk & 0xF) == 0)
    {
        paulaPeriod (c.index, c.n_period);
    }
    else
    {
        const auto* p = periods (c.n_finetune);
        const int n = numNotes();
        int i = 0;
        while (true)
        {
            if (c.n_period >= p[i]) break;
            if (++i >= n + 1) { i = n - 1; break; }
        }
        paulaPeriod (c.index, p[i]);
    }
}

void Player::vibrato2 (Chan& c)
{
    const int pos = (c.n_vibratopos >> 2) & 0x1F;
    const int type = c.n_wavecontrol & 3;
    int data;
    if (type == 0) data = kVibratoTable[pos];
    else if (type == 1) data = c.n_vibratopos < 128 ? (pos << 3) : 255 - (pos << 3);
    else data = 255;
    data = (data * (c.n_vibratocmd & 0xF)) >> 7;
    const int per = c.n_vibratopos < 128 ? c.n_period + data : c.n_period - data;
    paulaPeriod (c.index, per & 0xFFFF);
    c.n_vibratopos = (std::uint8_t) (c.n_vibratopos + ((c.n_vibratocmd >> 2) & 0x3C));
}

void Player::tremolo (Chan& c)
{
    if ((c.n_cmd & 0x0F) > 0) c.n_tremolocmd = (std::uint8_t) ((c.n_tremolocmd & 0xF0) | (c.n_cmd & 0x0F));
    if ((c.n_cmd & 0xF0) > 0) c.n_tremolocmd = (std::uint8_t) ((c.n_cmd & 0xF0) | (c.n_tremolocmd & 0x0F));
    const int pos = (c.n_tremolopos >> 2) & 0x1F;
    const int type = (c.n_wavecontrol >> 4) & 3;
    int data;
    if (type == 0) data = kVibratoTable[pos];
    else if (type == 1) data = c.n_vibratopos < 128 ? (pos << 3) : 255 - (pos << 3);   // PT bug kept: reads the vibrato position
    else data = 255;
    data = (data * (c.n_tremolocmd & 0xF)) >> 6;
    int vol;
    if (c.n_tremolopos < 128) vol = std::min (64, c.n_volume + data);
    else vol = std::max (0, c.n_volume - data);
    paulaVolume (c.index, vol);
    c.n_tremolopos = (std::uint8_t) (c.n_tremolopos + ((c.n_tremolocmd >> 2) & 0x3C));
}

void Player::sampleOffset (Chan& c)
{
    if ((c.n_cmd & 0xFF) > 0) c.n_sampleoffset = (std::uint8_t) (c.n_cmd & 0xFF);
    const std::uint16_t off = (std::uint16_t) (c.n_sampleoffset << 7);
    if (off < c.n_length)
    {
        c.n_length = (std::uint16_t) (c.n_length - off);
        c.n_start += (std::int64_t) off << 1;
        c.offsetApplied += (std::uint32_t) off << 1;
    }
    else
    {
        c.n_length = 1;
    }
}

void Player::eCommands (Chan& c)
{
    const int e = (c.n_cmd & 0x00F0) >> 4;
    const int x = c.n_cmd & 0xF;
    switch (e)
    {
        case 0x0:   // LED filter
            if (tickCount == 0) ledOn = (x & 1) == 0;
            return;
        case 0x1: if (tickCount == 0) { lowMask = 0xF; portaUp (c); } return;
        case 0x2: if (tickCount == 0) { lowMask = 0xF; portaDown (c); } return;
        case 0x3: c.n_glissfunk = (std::uint8_t) ((c.n_glissfunk & 0xF0) | x); return;
        case 0x4: c.n_wavecontrol = (std::uint8_t) ((c.n_wavecontrol & 0xF0) | x); return;
        case 0x5: c.n_finetune = (std::uint8_t) x; return;
        case 0x6: jumpLoop (c); return;
        case 0x7: c.n_wavecontrol = (std::uint8_t) ((x << 4) | (c.n_wavecontrol & 0xF)); return;
        case 0x8: return;   // Karplus-Strong: almost always a demo sync marker; off as in pt2-clone
        case 0xE: if (tickCount == 0 && pattDelTime2 == 0) pattDelTime = (std::uint8_t) (x + 1); return;
        case 0x9:
            if (x > 0)
            {
                if (tickCount == 0 && (c.n_note & 0xFFF) > 0) return;
                if (tickCount % x == 0) doRetrg (c);
            }
            return;
        case 0xA: if (tickCount == 0) { c.n_volume = (std::int8_t) std::min (64, c.n_volume + x); } return;
        case 0xB: if (tickCount == 0) { c.n_volume = (std::int8_t) std::max (0, c.n_volume - x); } return;
        case 0xC: if (tickCount == x) c.n_volume = 0; return;
        case 0xD: if (tickCount == x && (c.n_note & 0xFFF) > 0) doRetrg (c); return;
        case 0xF:
            if (tickCount == 0)
            {
                c.n_glissfunk = (std::uint8_t) ((x << 4) | (c.n_glissfunk & 0xF));
                if ((c.n_glissfunk & 0xF0) > 0) updateFunk (c);
            }
            return;
        default: return;
    }
}

void Player::checkMoreEffects (Chan& c)
{
    const int cmd = (c.n_cmd & 0x0F00) >> 8;
    switch (cmd)
    {
        case 0x9: sampleOffset (c); return;
        case 0xB:
            modPos = (c.n_cmd & 0xFF) - 1;
            pBreakPosition = 0;
            posJumpAssert = true;
            return;
        case 0xD:
            pBreakPosition = (std::int8_t) ((((c.n_cmd & 0xF0) >> 4) * 10) + (c.n_cmd & 0x0F));
            if ((std::uint8_t) pBreakPosition > 63) pBreakPosition = 0;
            posJumpAssert = true;
            return;
        case 0xE: eCommands (c); return;
        case 0xF:
            if ((c.n_cmd & 0xFF) > 0)
            {
                if ((c.n_cmd & 0xFF) < 32 || vblank) speed = c.n_cmd & 0xFF;
                else ciaSetBPM = c.n_cmd & 0xFF;   // the CIA takes the new value one tick later
            }
            else
            {
                stopSong = true;
            }
            return;
        case 0xC:
            c.n_volume = (std::int8_t) (c.n_cmd & 0xFF);
            if ((std::uint8_t) c.n_volume > 64) c.n_volume = 64;
            return;
        default: break;
    }
    paulaPeriod (c.index, c.n_period);
}

void Player::chkefx2 (Chan& c)
{
    updateFunk (c);
    if ((c.n_cmd & 0xFFF) == 0) return;
    const int cmd = (c.n_cmd & 0x0F00) >> 8;
    switch (cmd)
    {
        case 0x0: arpeggio (c); return;
        case 0x1: portaUp (c); return;
        case 0x2: portaDown (c); return;
        case 0x3:
            if ((c.n_cmd & 0xFF) > 0) { c.n_toneportspeed = (std::uint8_t) (c.n_cmd & 0xFF); c.n_cmd &= 0xFF00; }
            tonePortNoChange (c);
            return;
        case 0x4:
            if ((c.n_cmd & 0x0F) > 0) c.n_vibratocmd = (std::uint8_t) ((c.n_vibratocmd & 0xF0) | (c.n_cmd & 0x0F));
            if ((c.n_cmd & 0xF0) > 0) c.n_vibratocmd = (std::uint8_t) ((c.n_cmd & 0xF0) | (c.n_vibratocmd & 0x0F));
            vibrato2 (c);
            return;
        case 0x5: tonePortNoChange (c); volumeSlide (c); return;
        case 0x6: vibrato2 (c); volumeSlide (c); return;
        case 0xE: eCommands (c); return;
        default: break;
    }
    paulaPeriod (c.index, c.n_period);
    if (cmd == 0x7) tremolo (c);
    else if (cmd == 0xA) volumeSlide (c);
}

void Player::checkEffects (Chan& c)
{
    chkefx2 (c);
    if (((c.n_cmd & 0x0F00) >> 8) != 0x7) paulaVolume (c.index, c.n_volume);
}

void Player::setPeriod (Chan& c)
{
    const int note = c.n_note & 0xFFF;
    if (extended)
    {
        const auto* p0 = periods (0);
        int i = 0;
        for (; i < 60; ++i) if (note >= p0[i]) break;
        c.n_period = periods (c.n_finetune)[std::min (i, 60)];
        if (c.n_period == 0) c.n_period = (std::int16_t) note;   // beyond the table: play the raw period
    }
    else
    {
        int i = 0;
        for (; i < 37; ++i) if (note >= kPeriodTable[i]) break;
        c.n_period = kPeriodTable[c.n_finetune * 37 + i];   // i = 37 is safe (padding)
    }

    if ((c.n_cmd & 0xFF0) != 0xED0)
    {
        dmaOff (c.index);
        if ((c.n_wavecontrol & 0x04) == 0) c.n_vibratopos = 0;
        if ((c.n_wavecontrol & 0x40) == 0) c.n_tremolopos = 0;
        paulaLength (c.index, c.n_length);
        paulaData (c.index, c.n_start);
        if (c.n_start < 0)
        {
            c.n_loopstart = -1;
            paulaLength (c.index, 1);
            c.n_replen = 1;
        }
        paulaPeriod (c.index, c.n_period);
        dmaconTemp |= 1u << c.index;
    }
    checkMoreEffects (c);
}

void Player::playVoice (Chan& c)
{
    if (c.n_note == 0 && c.n_cmd == 0) paulaPeriod (c.index, c.n_period);

    const Cell note = fetch (modPattern, row, c.index);
    c.n_note = (std::int16_t) note.period;
    c.n_cmd = (std::uint16_t) ((note.effect << 8) | note.param);

    if (note.sample >= 1 && note.sample <= mod->numSamples)
    {
        const int s = note.sample - 1;
        const auto& smp = mod->samples[(std::size_t) s];
        c.n_samplenum = (std::uint8_t) s;
        c.sampleShown = note.sample;
        const std::uint32_t skip = sampleSkip[(std::size_t) s];
        c.n_start = sampleBase[(std::size_t) s] < 0 ? -1 : sampleBase[(std::size_t) s] + skip;
        c.offsetApplied = 0;
        c.n_finetune = (std::uint8_t) (smp.finetuneByte & 0xF);
        c.n_volume = (std::int8_t) smp.volume();
        c.n_length = (std::uint16_t) ((smp.lengthBytes() - skip) >> 1);
        c.n_replen = std::max<std::uint16_t> (1, smp.loopLengthWords);
        // ProTracker applies the loop start even to a non-looping sample (it shortens it);
        // FastTracker-style files ignore an unused loop start.
        std::uint16_t repeat = mod->layout == Layout::SoundTracker15 ? 0 : smp.loopStartWords;
        if (! smp.looped()) { c.n_replen = 1; if (extended) repeat = 0; }
        if (repeat > 0)
        {
            c.n_loopstart = c.n_start + ((std::int64_t) repeat << 1);
            c.n_wavestart = c.n_loopstart;
            c.n_length = (std::uint16_t) (repeat + c.n_replen);
        }
        else
        {
            c.n_loopstart = c.n_start;
            c.n_wavestart = c.n_start;
            if (mod->layout == Layout::SoundTracker15 && smp.looped()) c.n_length = c.n_replen;
        }
        if (c.n_length == 0 || c.n_start < 0)
        {
            c.n_loopstart = c.n_wavestart = 0;   // the silent page
            if (c.n_start < 0) c.n_start = 0;
        }
    }

    if ((c.n_note & 0xFFF) > 0)
    {
        if ((c.n_cmd & 0xFF0) == 0xE50)
        {
            c.n_finetune = (std::uint8_t) (c.n_cmd & 0xF);
            setPeriod (c);
        }
        else
        {
            const int cmd = (c.n_cmd & 0x0F00) >> 8;
            if (cmd == 3 || cmd == 5)
            {
                setTonePorta (c);
                checkMoreEffects (c);
            }
            else if (cmd == 9)
            {
                checkMoreEffects (c);
                setPeriod (c);
            }
            else
            {
                setPeriod (c);
            }
        }
    }
    else
    {
        checkMoreEffects (c);
    }
}

void Player::setDMA()
{
    for (int i = 0; i < numCh; ++i)
        if (dmaconTemp & (1u << i))
        {
            dmaOn (i);
            auto& c = ch[(std::size_t) i];
            if (onTrigger)
            {
                Trigger t;
                t.channel = i; t.sample = c.n_samplenum + 1; t.period = c.n_period; t.notePeriod = c.n_note & 0xFFF;
                t.volume = c.n_volume; t.order = curOrder; t.row = curRow; t.offsetBytes = c.offsetApplied;
                onTrigger (t);
            }
        }
    for (int i = 0; i < numCh; ++i)
    {
        paulaData (i, ch[(std::size_t) i].n_loopstart);
        paulaLength (i, ch[(std::size_t) i].n_replen);
    }
}

void Player::nextPosition()
{
    row = pBreakPosition;
    pBreakPosition = 0;
    posJumpAssert = false;
    modPos = (modPos + 1) & 127;
    if (modPos >= mod->songLength)
    {
        modPos = 0;
        wrapPending = true;
        if (mod->layout != Layout::SoundTracker15 && ! (mod->channels == 4 && ! extended)
            && mod->restartByte < mod->songLength && mod->restartByte != 0x7F && mod->restartByte != 0x78)
            modPos = mod->restartByte;   // FastTracker-style files honour the restart position
    }
    const int before = modPos;
    modPos = resolveOrder (modPos);
    if (modPos < before) wrapPending = true;
    modPattern = mod->orders[(std::size_t) modPos];
}

int Player::resolveOrder (int pos)
{
    // order entries that point past the stored patterns (0xFE / 0xFF markers) are skipped
    for (int guard = 0; guard < 129; ++guard)
    {
        if (pos >= mod->songLength || pos < 0) pos = 0;
        if (mod->orders[(std::size_t) pos] < mod->numPatterns()) return pos;
        ++pos;
    }
    return 0;
}

double Player::tickSeconds() const
{
    if (hostTempo) return beatsPerTick() * 60.0 / hostBpm;
    if (vblank) return (313.0 * 227.0) / kPaulaClock;
    return 1.0 / ciaBpmToHz (bpm);
}

bool Player::tick()
{
    if (mod == nullptr) return false;

    if (ciaSetBPM != -1)
    {
        bpm = ciaSetBPM;
        ciaSetBPM = -1;
    }

    // time spent in this tick
    {
        const double bpt = beatsPerTick();
        songBeats += bpt;
        songSeconds += tickSeconds();
    }
    ++ticksDone;

    ++tickCount;
    const bool readNewNote = tickCount >= speed;
    if (readNewNote) tickCount = 0;

    if (readNewNote)
    {
        if (pattDelTime2 == 0)
        {
            dmaconTemp = 0;
            curOrder = modPos;
            curRow = row;
            curPattern = modPattern;
            visited[(std::size_t) (modPos & 127)].set ((std::size_t) (row & 63));
            const int posBefore = modPos;
            rowLoopJumped = rowLoopSeen = false;
            for (int i = 0; i < numCh; ++i)
            {
                auto& c = ch[(std::size_t) i];
                playVoice (c);
                paulaVolume (i, c.n_volume);
            }
            if (rowLoopJumped)
            {
                // a loop jump cancels every break / jump on the same row
                posJumpAssert = false;
                modPos = posBefore;
                pBreakPosition = (std::int8_t) gLoopRow;
                pBreakFlag = true;
            }
            setDMA();
        }
        else
        {
            for (int i = 0; i < numCh; ++i) checkEffects (ch[(std::size_t) i]);
        }

        ++row;
        if (pattDelTime > 0) { pattDelTime2 = pattDelTime; pattDelTime = 0; }
        if (pattDelTime2 > 0)
        {
            if (--pattDelTime2 > 0) --row;
        }
        if (pBreakFlag)
        {
            row = pBreakPosition;
            pBreakPosition = 0;
            pBreakFlag = false;
        }
        if (row >= 64 || posJumpAssert) nextPosition();
    }
    else
    {
        for (int i = 0; i < numCh; ++i) checkEffects (ch[(std::size_t) i]);
        if (posJumpAssert) nextPosition();
    }

    if (stopSong) ended = true;

    // the next row was already played once: the song has looped
    if (pattDelTime2 == 0 && tickCount == speed - 1 && (wrapPending || visited[(std::size_t) (modPos & 127)].test ((std::size_t) (row & 63))))
    {
        ended = true;
        wrapPending = false;
    }

    return ! ended;
}

double Player::samplesPerTick() const
{
    return tickSeconds() * sr;
}

//==============================================================================
// Paula
void Player::nextSample (Voice& v, bool doBlep)
{
    if (v.sampleCounter == 0)
    {
        if (! v.justStarted)
        {
            if (--v.lenCounter == 0)
            {
                v.lenCounter = v.storedLen;
                v.loc = v.storedLoc;
                v.finished = v.storedLen <= 1;
            }
        }
        v.justStarted = false;
        if (v.loc + 1 >= mem.size()) v.loc = 0;
        v.dat[0] = mem[v.loc++];
        v.dat[1] = mem[v.loc++];
        v.sampleCounter = 2;
    }

    const float s = (float) v.dat[0] * v.storedVol;
    if (doBlep && s != v.blep.lastValue)
    {
        if (v.blepDelta > v.blepPhase)
            v.blep.add (v.blepPhase / v.blepDelta, v.blep.lastValue - s);
        v.blep.lastValue = s;
    }
    v.sample = s;
    v.dat[0] = v.dat[1];
    --v.sampleCounter;
    if (interp == Interp::Linear) v.next = peekNext (v) * v.storedVol;
}

float Player::peekNext (const Voice& v) const
{
    if (v.sampleCounter > 0) return (float) v.dat[0];
    std::uint32_t loc = v.loc;
    if (v.lenCounter <= 1) loc = v.storedLoc;
    return (float) memAt (loc);
}

void Player::mixVoices (float* L, float* R, float* const* stems, int n)
{
    const float gainBase = 2.0f / (float) std::max (2, numCh);   // every channel at full scale = 1.0 per side, never clips
    const float sepL = 0.5f + 0.5f * stereo, sepR = 0.5f - 0.5f * stereo;
    const bool led = ledMode == Led::On || (ledMode == Led::Auto && ledOn);

    for (int i = 0; i < numCh; ++i)
    {
        auto& v = voices[(std::size_t) i];
        float* stem = stems != nullptr ? stems[i] : nullptr;
        const bool silent = ! v.active || muted[(std::size_t) i];
        if (silent)
        {
            if (stem != nullptr) std::fill (stem, stem + n, 0.0f);
            // keep time moving for a muted voice so un-muting is seamless
            if (v.active) skipVoice (v, n);
            v.peak *= 0.5f;
            continue;
        }
        const float gl = gainBase * (kLeft[i] ? sepL : sepR);
        const float gr = gainBase * (kLeft[i] ? sepR : sepL);
        float peak = 0.0f;
        for (int j = 0; j < n; ++j)
        {
            if (v.nextStage)
            {
                v.nextStage = false;
                nextSample (v, interp == Interp::Amiga);
            }
            float s = v.sample;
            if (interp == Interp::Linear) s = v.sample + (v.next - v.sample) * std::min (1.0f, v.phase);
            if (interp == Interp::Amiga && v.blep.samplesLeft > 0) s = v.blep.run (s);
            peak = std::max (peak, std::abs (s));
            if (stem != nullptr) stem[j] = s * gainBase;
            else { L[j] += s * gl; R[j] += s * gr; }

            v.phase += v.delta;
            while (v.phase >= 1.0f)   // more than one Paula step per output sample at very high pitches
            {
                v.phase -= 1.0f;
                v.blepPhase = v.phase;
                v.blepDelta = v.delta;
                v.delta = v.storedDelta;
                if (v.nextStage) nextSample (v, false);
                v.nextStage = true;
            }
        }
        v.peak = std::max (peak * 128.0f / 127.0f, v.peak * 0.6f);
    }

    if (stems != nullptr)
    {
        for (int i = 0; i < numCh; ++i)
            for (int j = 0; j < n; ++j) stems[i][j] = stemFilter[(std::size_t) i].process (stems[i][j], led);
    }
    else
    {
        for (int j = 0; j < n; ++j)
        {
            L[j] = mixFilter[0].process (L[j], led);
            R[j] = mixFilter[1].process (R[j], led);
        }
    }
}

// k word fetches without reading the data (the DMA counter and pointer only)
void Player::fastFetch (Voice& v, std::uint64_t k)
{
    if (k == 0) return;
    if (v.justStarted) { v.justStarted = false; v.loc += 2; --k; }
    if (k == 0) return;
    const std::uint64_t L = v.lenCounter == 0 ? 65536u : v.lenCounter;
    if (k < L)
    {
        v.lenCounter = (std::uint16_t) (L - k);
        v.loc += (std::uint32_t) (2 * k);
    }
    else
    {
        k -= L;
        const std::uint64_t SL = v.storedLen == 0 ? 65536u : v.storedLen;
        v.finished = v.storedLen <= 1;
        k %= SL;   // whole passes through the loop change nothing
        v.lenCounter = (std::uint16_t) (SL - k);
        v.loc = v.storedLoc + 2u + (std::uint32_t) (2 * k);
    }
    if (v.loc + 1 >= mem.size()) v.loc = 0;
}

void Player::skipVoice (Voice& v, int n)
{
    if (! v.active || n <= 0) return;
    if (v.nextStage) { v.nextStage = false; nextSample (v, false); }
    double ph = (double) v.phase + (double) v.delta * (double) n;
    const auto crossings = (std::uint64_t) std::floor (ph);
    ph -= (double) crossings;
    if (crossings > 0)
    {
        v.delta = v.storedDelta;
        // crossings - 1 sample steps happen now, the last one stays pending (nextStage)
        std::uint64_t steps = crossings - 1;
        while (steps > 0 && v.sampleCounter > 0) { nextSample (v, false); --steps; }
        if (steps > 4)
        {
            const std::uint64_t words = steps / 2 - 1;   // keep the last word (and odd byte) for real fetches
            fastFetch (v, words);
            steps -= words * 2;
        }
        while (steps-- > 0) nextSample (v, false);
        v.nextStage = true;
    }
    v.phase = (float) ph;
    v.blepPhase = 0.0f;
    v.blepDelta = v.delta;
    v.blep = Blep {};
    v.blep.lastValue = v.sample;
}

void Player::render (float* L, float* R, int n)
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    if (mod == nullptr) return;
    int pos = 0;
    while (pos < n)
    {
        if (tickRemain <= 0.0)
        {
            tick();
            tickRemain += samplesPerTick();
        }
        const int m = std::min (n - pos, std::max (1, (int) std::ceil (tickRemain)));
        mixVoices (L + pos, R + pos, nullptr, m);
        tickRemain -= m;
        pos += m;
    }
}

int Player::renderTick (float* L, float* R, int cap)
{
    if (mod == nullptr || cap <= 0) return 0;
    tick();
    tickRemain += samplesPerTick();
    const int m = std::min (cap, std::max (1, (int) std::ceil (tickRemain)));
    std::fill (L, L + m, 0.0f);
    std::fill (R, R + m, 0.0f);
    mixVoices (L, R, nullptr, m);
    tickRemain -= m;
    return m;
}

void Player::renderStems (float* const* stems, int n)
{
    for (int i = 0; i < kMaxChannels; ++i) if (stems[i] != nullptr) std::fill (stems[i], stems[i] + n, 0.0f);
    if (mod == nullptr) return;
    int pos = 0;
    float* s[kMaxChannels] {};
    while (pos < n)
    {
        if (tickRemain <= 0.0)
        {
            tick();
            tickRemain += samplesPerTick();
        }
        const int m = std::min (n - pos, std::max (1, (int) std::ceil (tickRemain)));
        for (int i = 0; i < numCh; ++i) s[i] = stems[i] + pos;
        mixVoices (nullptr, nullptr, s, m);
        tickRemain -= m;
        pos += m;
    }
}

void Player::renderTail (float* L, float* R, float* const* stems, int n)
{
    if (stems != nullptr)
    {
        float* st[kMaxChannels] {};
        for (int i = 0; i < kMaxChannels; ++i) st[i] = stems[i];
        mixVoices (nullptr, nullptr, st, n);
        return;
    }
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    mixVoices (L, R, nullptr, n);
}

void Player::skip (int n)
{
    if (mod == nullptr) return;
    int pos = 0;
    while (pos < n)
    {
        if (tickRemain <= 0.0)
        {
            tick();
            tickRemain += samplesPerTick();
        }
        const int m = std::min (n - pos, std::max (1, (int) std::ceil (tickRemain)));
        for (int i = 0; i < numCh; ++i) skipVoice (voices[(std::size_t) i], m);
        tickRemain -= m;
        pos += m;
    }
}

bool Player::seekBeats (double target)
{
    return seekTo (target, true);
}

bool Player::seekSeconds (double target)
{
    return seekTo (target, false);
}

bool Player::seekTo (double target, bool inBeats)
{
    restart();
    target = std::max (0.0, target);
    // play ticks silently with the same sample counts render() would use;
    // the tick that contains the target is entered part way
    for (std::uint64_t guard = 0; guard < 4000000; ++guard)
    {
        const double start = inBeats ? songBeats : songSeconds;
        tick();
        const double end = inBeats ? songBeats : songSeconds;
        tickRemain += samplesPerTick();
        const int m = std::max (1, (int) std::ceil (tickRemain));
        if (end > target)
        {
            const double frac = end > start ? (target - start) / (end - start) : 0.0;
            const int done = std::min (m, (int) std::lround (frac * tickRemain));
            for (int i = 0; i < numCh; ++i) skipVoice (voices[(std::size_t) i], done);
            tickRemain -= done;
            return true;
        }
        for (int i = 0; i < numCh; ++i) skipVoice (voices[(std::size_t) i], m);
        tickRemain -= m;
    }
    return false;
}

bool Player::seekPosition (int order, int r)
{
    restart();
    if (order <= 0 && r <= 0) return true;
    // play silently until the replayer is about to read (order, row)
    for (std::uint64_t guard = 0; guard < 4000000; ++guard)
    {
        if (tickCount == speed - 1 && pattDelTime2 == 0 && modPos == order && row == r && tickRemain <= 0.0) return true;
        if (ended) break;
        tick();
        tickRemain += samplesPerTick();
        const int m = std::max (1, (int) std::ceil (tickRemain));
        for (int i = 0; i < numCh; ++i) skipVoice (voices[(std::size_t) i], m);
        tickRemain -= m;
    }
    // never reached by the song: start there directly (as ProTracker does)
    restart (order, r);
    return false;
}

ChannelView Player::channelView (int c) const
{
    ChannelView v;
    if (c < 0 || c >= numCh) return v;
    const auto& vo = voices[(std::size_t) c];
    v.period = vo.period;
    v.volume = vo.volume;
    v.sample = ch[(std::size_t) c].sampleShown;
    v.active = vo.active;
    v.audible = vo.active && ! vo.finished;
    v.level = std::min (1.0f, vo.peak * 2.0f);
    return v;
}

Player::Scan Player::scan (const Module& m, double rate, int timing)
{
    Scan s;
    Player p;
    p.prepare (rate);
    p.setModule (&m);
    if (timing >= 0) p.setTiming ((Timing) timing);
    s.orderSeconds.assign ((std::size_t) m.songLength, -1.0);
    int lastOrder = -1, lastRow = -1;
    for (std::uint64_t guard = 0; guard < 2000000; ++guard)
    {
        const double t = p.songSeconds;
        p.tick();
        if (p.curOrder != lastOrder || p.curRow != lastRow)
        {
            if (p.curOrder < m.songLength && s.orderSeconds[(std::size_t) p.curOrder] < 0.0) s.orderSeconds[(std::size_t) p.curOrder] = t;
            lastOrder = p.curOrder;
            lastRow = p.curRow;
            ++s.rowsPlayed;
        }
        if (p.ended) break;
    }
    s.seconds = p.songSeconds;
    s.beats = p.songBeats;
    s.loops = ! p.stopSong;
    return s;
}

Timing Player::detectTiming (const Module& m)
{
    if (m.vblankOnly) return Timing::VBlank;
    if (! m.highFxx || ! (m.tag == "M.K." || m.tag == "M!K!") || m.layout != Layout::Standard31) return Timing::Cia;

    // low and high Fxx on one row: a CIA tracker
    for (const auto& pat : m.patterns)
        for (int r = 0; r < kRows; ++r)
        {
            bool lo = false, hi = false;
            for (int c = 0; c < m.channels; ++c)
            {
                const auto& cl = pat[(std::size_t) (r * m.channels + c)];
                if (cl.effect == 0xF && cl.param > 0) (cl.param >= 0x20 ? hi : lo) = true;
            }
            if (lo && hi) return Timing::Cia;
        }

    // high Fxx only in the last two orders (silence padding): VBlank, unless F7D
    if (m.songLength >= 8)
    {
        auto highIn = [&] (int pat)
        {
            int v = 0;
            if (pat < 0 || pat >= m.numPatterns()) return 0;
            for (const auto& cl : m.patterns[(std::size_t) pat]) if (cl.effect == 0xF && cl.param >= 0x20) v = cl.param;
            return v;
        };
        const int threshold = m.songLength - 2;
        int i = 0;
        for (; i < threshold; ++i) if (highIn (m.orders[(std::size_t) i]) != 0) break;
        if (i == threshold)
            for (int k = m.songLength - 1; k >= threshold; --k)
            {
                const int f = highIn (m.orders[(std::size_t) k]);
                if (f == 0) continue;
                if (f == 0x7D) return Timing::Cia;
                return Timing::VBlank;
            }
    }

    // long songs: the wrong timing usually makes them much longer
    const auto cia = scan (m, 8000.0, (int) Timing::Cia);
    if (cia.seconds < 480.0) return Timing::Cia;
    const auto vbl = scan (m, 8000.0, (int) Timing::VBlank);
    return vbl.seconds < cia.seconds ? Timing::VBlank : Timing::Cia;
}

} // namespace modport
