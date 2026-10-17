// TFMovementCore.h
//
// Engine-independent, clean-room reimplementation of Titanfall 2 pilot movement.
// Written ONLY from the reverse-engineering notes in docs/ (TITANFALL2_MOVEMENT.md and the
// detailed notes it links). No game code was copied; the doc pseudocode is a spec that is
// re-expressed here in a different structure and with our own names.
//
// This header deliberately uses no Unreal types or headers so the logic can be compiled and
// tested standalone (plain C++17).
//
// Units: Titanfall/Source units everywhere. 1 unit = 1 inch, speeds in u/s, time in seconds.
// Axes:  Source convention. +X forward, +Y LEFT, +Z up (right-handed). View pitch is positive
//        when looking DOWN. The Unreal component converts at the boundary.
// Origin: the player origin is the bottom-centre of the hull (feet), as in Source.
//
// Tag legend used on every tunable (see examples/unreal/README.md):
//   [D] shipped multiplayer pilot value from pilot_base.set / pilot_mp.set (TITANFALL2_MOVEMENT.md sec.0)
//   [R] code default or ConVar default read from server.dll
//   [C] computed from [R]/[D] values
//   [I] inferred / assumed (best guess, see docs/ASSUMPTIONS.md)
//   [?] unknown in the docs; a placeholder value is used
//
// Anti-tamper (Denuvo_CheckWallrun / Denuvo_SlowSpeed, doc sec.12) is intentionally omitted.

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Small vector math
// ---------------------------------------------------------------------------------------------
struct TFVec3
{
	float X = 0.f;
	float Y = 0.f;
	float Z = 0.f;

	TFVec3() = default;
	TFVec3(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

	TFVec3 operator+(const TFVec3& O) const { return TFVec3(X + O.X, Y + O.Y, Z + O.Z); }
	TFVec3 operator-(const TFVec3& O) const { return TFVec3(X - O.X, Y - O.Y, Z - O.Z); }
	TFVec3 operator*(float S) const { return TFVec3(X * S, Y * S, Z * S); }
	TFVec3 operator/(float S) const { return TFVec3(X / S, Y / S, Z / S); }
	TFVec3 operator-() const { return TFVec3(-X, -Y, -Z); }
	TFVec3& operator+=(const TFVec3& O) { X += O.X; Y += O.Y; Z += O.Z; return *this; }
	TFVec3& operator-=(const TFVec3& O) { X -= O.X; Y -= O.Y; Z -= O.Z; return *this; }
	TFVec3& operator*=(float S) { X *= S; Y *= S; Z *= S; return *this; }

	float Dot(const TFVec3& O) const { return X * O.X + Y * O.Y + Z * O.Z; }
	float Dot2D(const TFVec3& O) const { return X * O.X + Y * O.Y; }
	TFVec3 Cross(const TFVec3& O) const { return TFVec3(Y * O.Z - Z * O.Y, Z * O.X - X * O.Z, X * O.Y - Y * O.X); }
	float LengthSq() const { return X * X + Y * Y + Z * Z; }
	float Length() const { return std::sqrt(LengthSq()); }
	float Length2DSq() const { return X * X + Y * Y; }
	float Length2D() const { return std::sqrt(Length2DSq()); }
	TFVec3 Flat() const { return TFVec3(X, Y, 0.f); }

	// Returns the unit vector, or zero when the length is below Eps.
	TFVec3 Normalized(float Eps = 1e-6f) const
	{
		const float L = Length();
		return (L > Eps) ? (*this / L) : TFVec3();
	}
	TFVec3 Normalized2D(float Eps = 1e-6f) const
	{
		const float L = Length2D();
		return (L > Eps) ? TFVec3(X / L, Y / L, 0.f) : TFVec3();
	}
	bool IsNearlyZero(float Eps = 1e-6f) const { return LengthSq() <= Eps * Eps; }

	static TFVec3 Up() { return TFVec3(0.f, 0.f, 1.f); }
	static TFVec3 Lerp(const TFVec3& A, const TFVec3& B, float T) { return A + (B - A) * T; }
};

inline TFVec3 operator*(float S, const TFVec3& V) { return V * S; }

// ---------------------------------------------------------------------------------------------
// World interface (implemented by the engine layer)
// ---------------------------------------------------------------------------------------------
struct TFTraceResult
{
	float Fraction = 1.f;       // 0..1 of the requested move that was completed
	TFVec3 EndPos;              // hull origin (feet) / line point where the trace stopped
	TFVec3 Normal;              // surface normal of the blocking surface (zero when nothing was hit)
	bool bStartSolid = false;   // the trace started inside geometry
	int64_t HitEntityId = 0;    // 0 = world / static geometry; otherwise an engine id
	bool bNoGrapple = false;    // surface disallows grapple attachment (trigger_no_grapple / flag 0x800)

	bool Hit() const { return bStartSolid || Fraction < 1.f; }
};

// A zipline rope as a polyline of nodes. The sag of each segment is given by the rope height at
// the segment middle; the core turns it into the A*t^2+B*t+C curve the game stores per rope node.
struct TFZiplineRope
{
	int Id = -1;
	std::vector<TFVec3> Nodes;        // rope node positions (TF units, world space)
	std::vector<float> SegmentMidZ;   // optional, size Nodes-1: rope Z at segment t=0.5; empty = straight chords
	float MoveSpeedScale = 1.f;       // m_ziplineMoveSpeedScale, per rope. [?] map data, default 1
	float AutoDetachDistance = 0.f;   // m_ziplineAutoDetachDistance of the end nodes (TF units)
};

enum class ETFMoveEvent : uint8_t
{
	Jump,
	SkipJump,
	DoubleJump,
	WallJump,
	SlideJump,
	Land,
	Impact,
	SlideStart,
	SlideEnd,
	WallRunStart,
	WallRunEnd,
	WallHangStart,
	MantleStart,
	MantleEnd,
	GrappleFire,
	GrappleAttach,
	GrapplePullStart,
	GrappleDetach,
	ZiplineAttach,
	ZiplineDetach,
	Dodge,
};

struct ITFWorld
{
	virtual ~ITFWorld() = default;

	// Sweep the player hull (standing or crouched) from Start to End (feet positions).
	virtual TFTraceResult SweepHull(const TFVec3& Start, const TFVec3& End, bool bCrouchedHull) = 0;
	// Plain line trace (visibility).
	virtual TFTraceResult TraceLine(const TFVec3& Start, const TFVec3& End) = 0;
	// Simulation time in seconds (gpGlobals->curtime). Must advance by dt between ticks.
	virtual float Now() = 0;

	// Ziplines in range of Center. Fills OutRopes with up to MaxRopes pointers, returns count.
	virtual int QueryZiplines(const TFVec3& /*Center*/, float /*Radius*/, const TFZiplineRope** /*OutRopes*/, int /*MaxRopes*/) { return 0; }
	// Rope by id (pointer must stay valid for the tick). nullptr when the rope no longer exists.
	virtual const TFZiplineRope* GetZipline(int /*Id*/) { return nullptr; }
	// Whether a grappled entity is still a valid (alive) target. World geometry (id 0) is always valid.
	virtual bool IsGrappleTargetValid(int64_t /*EntityId*/) { return true; }
	// Optional notifications for FX / animation.
	virtual void OnMoveEvent(ETFMoveEvent /*Event*/, float /*Value*/) {}
};

// ---------------------------------------------------------------------------------------------
// Settings. Default member initializers are the SHIPPED MP pilot values (doc sec.0), falling back to
// code defaults where the .set files do not set a field. "@group" lines are read by a generator
// that produced the Unreal mirror struct (FTFPilotSettingsUE) - keep one field per line.
// ---------------------------------------------------------------------------------------------
enum class ETFWallrunAdsType : int32_t
{
	None = 0,      // ADS is cancelled while wallrunning
	WallHang = 1,  // ADS enters a wallhang
	Ads = 2,       // ADS just aims
};

struct TFPilotSettings
{
	// @group Ground
	float WalkSpeed = 173.5f;                       // [D] stand.speed (code default 120)
	float SprintSpeed = 260.f;                      // [D] stand.sprintspeed (code default 0)
	float CrouchSpeed = 80.f;                       // [D] crouch.speed (parser default 120 [I])
	float CrouchSprintSpeed = 0.f;                  // [R] crouch.sprintspeed parser default
	float Acceleration = 2500.f;                    // [R] stance acceleration
	float Deceleration = -1.f;                      // [R] -1 = fallback 0.6 x Acceleration; effective 1500 [C]
	float LowSpeed = -1.f;                          // [R] lowAcceleration threshold, -1 = off
	float LowAcceleration = -1.f;                   // [R] -1 = use Acceleration
	float SprintAcceleration = -1.f;                // [R] above run speed, -1 = use Acceleration
	float SprintDeceleration = -1.f;                // [R] above run speed, -1 = fallback
	float DecelFallbackFraction = 0.6f;             // [R] decel = 0.6 x accel when unset
	float SprintInputDivisor = 0.8f;                // [R] sprint divides stick input by 0.8 (x1.25)
	float SpeedScaleSide = 1.f;                     // [R] speedScaleSide
	float SpeedScaleBack = 1.f;                     // [R] speedScaleBack
	float StepHeight = 18.f;                        // [D] stepHeight (step size = copy of it [I])
	float StepTestDist = 10.f;                      // [R] stepTestDist
	float StandHullHeight = 72.f;                   // [D] 32x32x72 stand hull
	float CrouchHullHeight = 47.f;                  // [D] 32x32x47 crouch hull
	float HullRadius = 16.f;                        // [D] half of the 32-unit hull width
	float StandEyeHeight = 60.f;                    // [D] stand viewheight
	float CrouchEyeHeight = 32.f;                   // [D] crouch viewheight (pilot_mp lowers 38 -> 32)
	float ImpactSpeed = 380.f;                      // [D] impactSpeed (code default 580)
	float LandingFxFallSpeed = 80.f;                // [R] landing check fall speed for pilots
	float LeaveGroundUpSpeed = 140.f;               // [R] up-speed that leaves the ground
	float WalkableNormalZ = 0.7f;                   // [R] walkable normal z
	float AirSurfaceFriction = 0.25f;               // [R] m_surfaceFriction when leaving ground upward
	float AccelScale = 1.f;                         // [I] player+0x1600 global accel/decel scale, presumed 1
	float GroundFrictionScale = 1.f;                // [R] m_groundFrictionScale (default 1 [I])
	float PowerRegenRate = 10.f;                    // [R] classMods powerRegenRate (suit power /s)
	float SprintMinPower = 10.f;                    // [R] power needed to start sprinting
	float AdsBlendTime = 0.2f;                      // [?] ADS in/out time (weapon data, not in docs)
	float MoveSpeedScale = 1.f;                     // [R] m_playerMoveSpeedScale

	// @group Sprint
	bool bSprintEnabled = true;                     // [R] classMods sprint default 1
	bool bSprintToggle = false;                     // [I] input style; game input code not analysed
	float SprintViewOffset = -6.f;                  // [D] sprintViewOffset (code default -10)
	float SprintStartDelay = 0.2f;                  // [R] sprintStartDelay
	float SprintStartDuration = 0.8f;               // [D] sprintStartDuration (code default 0.3)
	float SprintTiltMaxRoll = 2.f;                  // [D] sprinttiltMaxRoll degrees (code default 10)

	// @group Jump
	bool bJumpEnabled = true;                       // [R] class key jump
	float JumpHeight = 60.f;                        // [D] jumpHeight -> launch sqrt(2*750*60) = 300 u/s [C], apex ~75 u [C]
	float GravityScale = 0.8f;                      // [D] gravityScale; used as entity gravity [I]
	bool bDoubleJump = true;                        // [D] doubleJump 1
	float SuperjumpMinHeight = 60.f;                // [D] superjumpMinHeight (code default 80); extra apex ~75 u [C]
	float SuperjumpMaxHeight = 60.f;                // [D] superjumpMaxHeight (unused: t is always 0 [R])
	float SuperjumpLimit = 1.f;                     // [R] superjumpLimit
	float SuperjumpHorzSpeed = 180.f;               // [D] superjumpHorzSpeed (code default 240)
	bool bSuperjumpCanUseAfterWallrun = true;       // [R] superjumpCanUseAfterWallrun
	bool bBoostEnabled = false;                     // [R] boostEnabled (jetpack class only)
	float BoostOnGroundSafety = 0.2f;               // [R] boostOnGroundSafety (second coyote window)
	float AirSpeed = 60.f;                          // [R] airSpeed (m_airSpeed holds it [I])
	float AirAcceleration = 500.f;                  // [R] airAcceleration
	bool bZeroAirSpeed = false;                     // [R] settings+0x1c8 forces air speed to 0
	float WallNudgeSpeed = 30.f;                    // [R] last-wall-normal nudge on jump/dodge

	// @group Slide
	bool bSlideEnabled = true;                      // [D] slide enabled
	float SlideDecel = 50.f;                        // [D] slideDecel (code default 160)
	float SlideWantToStopDecel = 400.f;             // [R] slideWantToStopDecel
	float SlideVelocityDecay = 0.7f;                // [D] slideVelocityDecay retained per s (code default 0.85)
	float SlideAccel = 50.f;                        // [R] slideAccel
	float SlideRequiredStartSpeed = 200.f;          // [R] slideRequiredStartSpeed
	float SlideStopSpeed = 125.f;                   // [R] slideStopSpeed
	float SlideMaxStopSpeed = 350.f;                // [R] slideMaxStopSpeed (Duck forces crouch above it)
	float SlideMaxJumpSpeed = 350.f;                // [R] slideMaxJumpSpeed
	float SlideSpeedBoost = 150.f;                  // [R] slideSpeedBoost
	float SlideSpeedBoostCap = 400.f;               // [R] slideSpeedBoostCap
	float SlideJumpHeight = 50.f;                   // [R] slideJumpHeight
	float SlideBoostStripWindow = 0.4f;             // [R] slide-jump boost strip window (s)
	float SlideJumpSnapBand = 0.85f;                // [R] slide-jump snap band lower bound
	float SlideBoostDecayStimFactor = 0.75f;        // [R] exponent factor (1 - 0.75*boost) [I that it is speed_boost]

	// @group Crouch
	bool bCrouchEnabled = true;                     // [R] crouch default 1
	float DuckTime = 0.4f;                          // [R] stand -> ducked
	float DuckTimeSliding = 0.2f;                   // [R] stand -> ducked while sliding
	float UnduckTime = 0.2f;                        // [R] ducked -> standing

	// @group Wallrun
	bool bWallrunEnabled = true;                    // [D] wallrun 1
	float WallrunTimeLimit = 1.75f;                 // [D] wallrun_timeLimit (code default 8)
	float WallrunHangTimeLimit = 4.f;               // [D] wallrun_hangTimeLimit (code default 9)
	bool bWallRunnerKit = false;                    // [D] pas_wall_runner equipped
	float WallRunnerKitTimeAdd = 2.5f;              // [D] "2.5++" read as add [I]
	float WallRunnerKitHangTimeAdd = 21.f;          // [D] "21++" read as add [I]
	float WallrunMaxSpeedHorizontal = 420.f;        // [D] (code default 450)
	float WallrunMaxSpeedVertical = 225.f;          // [D] (code default 300)
	float WallrunMaxSpeedHorizontalBackward = 50.f; // [R]
	float WallrunAccelerateHorizontal = 1500.f;     // [D]
	float WallrunAccelerateVertical = 360.f;        // [D] (code default 450)
	float WallrunJumpUpSpeed = 230.f;               // [D] (code default 320)
	float WallrunJumpOutwardSpeed = 205.f;          // [D] (code default 250)
	float WallrunJumpInputDirSpeed = 80.f;          // [D] (code default 100)
	float WallrunFriction = 4.f;                    // [R] wallrunFriction
	float WallrunFrictionScale = 1.f;               // [R] m_wallrunFrictionScale (default 1 [I])
	ETFWallrunAdsType WallrunAdsType = ETFWallrunAdsType::Ads; // [D] "ADS" (code default wallhang)
	bool bWallHangKit = false;                      // [D] pas_wallhang kit; enables WallHang when set [I]
	bool bWallstickEnabled = false;                 // [R] wallstickEnabled
	bool bWallrunSameWallAllowed = true;            // [R] wallrunSameWallAllowed
	float WallrunMinStartAirTime = 0.5f;            // [R] 0.5 s gate on m_onSlopeTime [I meaning]
	float WallrunMinNormalZ = -0.35f;               // [R] wall normal z lower bound
	float WallrunMaxNormalZ = 0.7f;                 // [R] wall normal z upper bound
	float WallrunProbeLowOffset = 16.f;             // [R] +16 probe height
	float WallrunTopProbeOffset = 32.f;             // [R] eye+32 wall-top probe
	float WallrunTopDecelMultiplier = 3.f;          // [R] 3 x wallrun_avoid_wall_top_decel
	float WallrunJumpIntoWallCut = 0.8f;            // [R] outward speed cut when pushing into the wall
	float WallrunJumpUpMultiplier = 1.5f;           // [R] vz = min(vz + 1.5*up, up)
	float WallrunLandUpSpeed = 140.f;               // [R] vz limit for landing check while wallrunning
	float WallrunCategorizeRestartTime = 1.f;       // [R] steep surface restarts a wallrun after 1 s
	float WallrunCategorizeLandTime = 0.5f;         // [R] floor ends a wallrun after 0.5 s
	float WallrunBackwardCos = -0.6f;               // [R] backward cap when wish*facing <= -0.6
	float WallrunViewTilt = 15.f;                   // [I] client roll; wallrun_maxViewTilt value
	float WallrunViewTiltSpeed = 6.f;               // [I] client roll speed; wallrun_viewTiltSpeed value
	bool bApplyWallRepel = false;                   // [?] ApplyWallRepel has no known caller (W6)

	// @group Mantle
	bool bAutomantleEnabled = true;                 // [D] mantle enabled
	float MantleAngleScale = 1.f;                   // [R] mantleAngleScale
	bool bMantleFollowsAnimation = false;           // [?] traversal_anim 1 path is model data; we use the begin->mid->end path [I]
	float MantleSlopeGateTime = 0.25f;              // [R] no mantle within 0.25 s of m_onSlopeTime unless jump held
	float MantlePressingForward = 0.3f;             // [R] forwardmove >= 0.3
	float MantleEpsilon = 0.03125f;                 // [R]
	float MantlePushDot = 0.1f;                     // [R] pushing threshold
	float MantlePullDot = -0.5f;                    // [R] pulling threshold
	float MantleWallrunIntoWallLimit = -100.f;      // [R] dot(vel, wallN) >= -100 while wallrunning

	// @group Dodge
	bool bDodgeEnabled = false;                     // [D] dodge 0 for MP pilots
	float DodgeSpeed = 750.f;                       // [R] dodgeSpeed
	float DodgePowerDrain = 33.f;                   // [R] dodgePowerDrain
	float DodgeDuration = 0.f;                      // [R] dodgeDuration (m_dodgingInAir length)
	float DodgeAnimDuration = 0.5f;                 // [R] dodgeAnimDuration
	float DodgeInterval = 0.6f;                     // [R] dodgeInterval
	float DodgeStopSpeed = 300.f;                   // [R] dodgeStopSpeed
	float DodgeHeight = 25.f;                       // [R] dodgeHeight
	float DodgeVerticalHeight = 200.f;              // [R] dodgeVerticalHeight
	float DodgePowerDelay = 0.f;                    // [R] dodgePowerDelay
	float DodgeKeepSpeedFrac = 0.f;                 // [R] dodgeKeepSpeedFrac

	// @group Hover
	bool bHoverEnabled = false;                     // [R] hoverEnabled (pas_ads_hover kit sets 1 [D])
	float HoverTightnessVertical = 0.15f;           // [R] half-life (s)
	float HoverTightnessHorizontal = 0.08f;         // [R] half-life (s)
	float HoverSafety = 0.25f;                      // [R] hoverSafety
	float AirSlowMoSpeed = 0.15f;                   // [D] airSlowmoSpeed (code default 1.0)

	// @group Grapple
	float GrappleDetachLengthMin = 33.f;            // [D]
	float GrappleDetachLengthMax = 50.f;            // [D]
	float GrappleDetachAwaySpeed = 500.f;           // [D] (code default 300)
	float GrappleGravityFracMin = 0.25f;            // [D] (code default 0)
	float GrappleGravityFracMax = 0.7f;             // [D] (code default 0.75)
	float GrappleDetachVerticalBoost = 200.f;       // [D]
	float GrappleDetachVerticalMaxSpeed = 200.f;    // [D]
	float GrappleDetachSpeedLoss = 300.f;           // [D] (code default 400)
	float GrappleDetachSpeedLossMin = 460.f;        // [D] pilot_mp (pilot_base 430)
	float GrappleDetachLowSpeedThreshold = 250.f;   // [D]
	float GrappleDetachLowSpeedMeleeThreshold = 100.f; // [R]
	float GrappleDetachLowSpeedTime = 1.5f;         // [D] in air
	float GrappleDetachLowSpeedWallTime = 1.2f;     // [D] wallrunning
	float GrappleDetachLowSpeedGroundTime = 0.7f;   // [D] on ground
	float GrappleDetachLowSpeedMeleeTime = 1.f;     // [R]
	float GrappleImpactVerticalBoost = 300.f;       // [D]
	float GrappleImpactVerticalMaxSpeed = 300.f;    // [D]
	float GrappleAirSpeedMax = 420.f;               // [D]
	float GrappleAirAccel = 650.f;                  // [D] (code default 1000)
	float GrapplePowerRegenDelay = 3.f;             // [D] (code default 1)
	float GrapplePowerRegenRate = 3.f;              // [D] per s (code default 17)
	bool bPowerCellKit = false;                     // [D] pas_power_cell
	float PowerCellRegenMultiplier = 1.35f;         // [D] grapple_power_regen_rate *1.35
	float GrappleMaxLength = 1100.f;                // [D] mp_ability_grapple.txt grapple_maxLength
	float GrappleMaxLengthVert = 1100.f;            // [D] grapple_maxLengthVert
	float GrapplePowerRequired = 50.f;              // [D] grapple_power_required
	float GrapplePowerUseRate = 50.f;               // [D] grapple_power_use_rate per s
	float GrappleLengthMargin = 50.f;               // [R] detach when len > max(range) + 50
	float GrappleMinLengthSpeedScale = 1.5f;        // [R] speed-scaled min length factor

	// @group Zipline
	float ZiplineSpeed = 600.f;                     // [D] (code default 800); ride cap = 600 x rope scale [C]
	float ZiplineAcceleration = 400.f;              // [R]
	float ZiplineJumpOffSpeed = 400.f;              // [R]
	float ZiplineJumpOffDampScale = 1.f;            // [R] 1 = velocity unchanged on dismount
	float MountZiplineTime = 0.5f;                  // [R] fallback; real time = viewmodel anim length [?]
	float UseZiplineCooldown = 1.f;                 // [R]
	bool bZiplineAutoAttachInAir = true;            // [I] trigger for Zipline_Find not in docs; also on Use

	// @group Misc
	float Health = 100.f;                           // [D] not used by movement
	float HealthShield = 200.f;                     // [D] use unknown [?]
	float Fov = 70.f;                               // [D] not used by movement
	float StimSeverity = 0.4f;                      // [D] sh_stim speed_boost severity
	float StimExtraTime = 0.5f;                     // [D] stim effect lasts duration + 0.5 s
	float StimFadeOut = 0.25f;                      // [I] last StatusEffect_AddTimed argument read as fade-out
	float SpeedBoostFactor = 2.f;                   // [R] SpeedScale x (1 + 2*boost)
};

// ConVars (registered defaults from server.dll, all [R] unless noted). Playlist overrides [?].
struct TFMovementConVars
{
	// @group ConVarsCore
	float sv_gravity = 750.f;                        // [R]
	float sv_maxvelocity = 12100.f;                  // [R] per-axis limit, uniform scale
	float MapBound = 65536.f;                        // [R] origin clamp
	float jump_graceperiod = 0.2f;                   // [R] coyote time
	float jump_keyboardgraceperiodmin = 0.2f;        // [R]
	float jump_keyboardgraceperiodmax = 0.5f;        // [R]
	float jump_keyboardgrace_strength = 0.7f;        // [R]
	float jump_keyboardgrace_max = 0.7f;             // [R]
	float skip_time = 0.1f;                          // [R]
	float skip_jump_height_fraction = 0.75f;         // [R]
	float skip_speed_retain = 450.f;                 // [R]
	float skip_speed_reduce = 12.f;                  // [R]
	bool skip_replenish_double_jump = true;          // [R]
	float superjump_min_height_fraction = 0.25f;     // [R]
	float superjump_min_power_use = 0.f;             // [R]
	float superjump_max_power_use = 100.f;           // [R] unused (t = 0)
	bool superjump_powerreset_onground = true;       // [R]
	bool superjump_limitreset_onwallrun = true;      // [R]
	float player_extraairaccelleration = 2.f;        // [R]
	float grapple_jumpFrac = 1.f;                    // [R]

	// @group ConVarsSlide
	bool slide_whileInAir = false;                   // [R]
	float slide_max_angle_dot = 0.6f;                // [R]
	float slide_boost_cooldown = 2.f;                // [R]
	float slide_step_velocity_reduction = 10.f;      // [R]
	bool slide_auto_stand = false;                   // [R]

	// @group ConVarsWallrun
	bool wallrun_enable = true;                      // [R]
	float wallrun_allowed_wall_dist = 13.f;          // [R]
	float wallrun_allowed_wall_dist_wallhang = 18.f; // [R]
	float wallrun_minAngle_air = 180.f;              // [R] full cone angle [I]
	float wallrun_angleChangeMinCos = 0.8f;          // [R]
	float wallrun_rotateMaxRate = 3.f;               // [R] rad/s [I]
	float wallrun_upWallBoost = 250.f;               // [R]
	float wallrun_upwardAutoPush = 0.65f;            // [R]
	float wallrun_gravityRampUpTime = 1.f;           // [R]
	float wallrun_strengthLossStart = 1000.f;        // [R]
	float wallrun_strengthLossEnd = 1000.f;          // [R]
	float wallrun_slipstarttime = 1.5f;              // [R]
	float wallrun_slipduration = 1.f;                // [R]
	float wallrun_noInputSlipFrac = 0.7f;            // [R]
	float wallrun_pushAwayFallOffTime = 0.05f;       // [R]
	float wallrun_fallAwaySpeed = 70.f;              // [R]
	float wallrun_avoid_wall_top_decel = 3000.f;     // [R]
	float wallrun_retry_interval = 0.07f;            // [R]
	float wallrun_sameWallDist = 100.f;              // [R]
	float wallrun_sameWallDot = 0.9f;                // [R]
	float wallrun_sameWallHeight = 0.f;              // [R]
	float wallrun_sameWallSlope = 0.f;               // [R]
	bool wallrun_repelEnable = true;                 // [R]
	float wallrun_repelTimeMin = 0.2f;               // [R]
	float wallrun_repelTimeMax = 0.4f;               // [R]
	float wallrun_repelSoftness = 5.f;               // [R] softness argument [I]
	float wallrun_hangStopTime = 0.5f;               // [R]
	float wallrun_hangslipstarttime = 3.f;           // [R]
	float wallrun_hangslipduration = 1.f;            // [R]

	// @group ConVarsMantle
	bool automantle_enable = true;                   // [R]
	float automantle_height = 80.f;                  // [R]
	float automantle_height_below = -10.f;           // [R]
	float automantle_height_level = 10.f;            // [R]
	float automantle_height_above = 30.f;            // [R]
	float automantle_searchdist = 5.f;               // [R]
	float automantle_forwarddist = 30.f;             // [R]
	float automantle_mindist = 18.f;                 // [R]
	float automantle_duration_below = 1.11f;         // [R]
	float automantle_duration_level = 1.f;           // [R]
	float automantle_duration_above = 0.5f;          // [R]
	float automantle_duration_high = 0.35f;          // [R]
	float automantle_rest_frac = 0.4f;               // [R]
	float automantle_rest_frac_below = 0.3f;         // [R]
	float automantle_maxangle_push = 50.f;           // [R] degrees
	float automantle_maxangle_view = 50.f;           // [R] degrees
	float automantle_wallrun_maxangle_view = 45.f;   // [R] degrees
	float automantle_jumpoff_duration = 0.1f;        // [R]
	float automantle_jumpoff_anim_maxfrac = 0.5f;    // [R]
	float automantle_backoff_anim_maxfrac = 0.7f;    // [R]
	float automantle_dangle_required_space = 60.f;   // [R]
	float automantle_gun_enable_height = 33.f;       // [R]
	float automantle_cooldown = 0.25f;               // [R]
	float traversal_cooldown = 0.5f;                 // [R]

	// @group ConVarsDodge
	bool dodge_no_movement_forward_enable = true;    // [R]
	bool dodge_vertical_enable = false;              // [R]
	float dodge_vertical_threshold = 0.1f;           // [R]
	float dodge_vertical_horzspeedscale = 0.5f;      // [R]
	bool dodge_vertical_in_air = true;               // [R]
	float sprint_powerdrain = 0.f;                   // [R] 0 = sprint is free
	float sprinttilt_accel = 35.f;                   // [R]
	float sprinttilt_maxvel = 2.f;                   // [R]
	float sprinttilt_turnrange = 120.f;              // [R]
	bool airslowmo_enabled = false;                  // [R]
	float airslowmo_enter_time = 0.25f;              // [R]
	float airslowmo_leave_time = 1.f;                // [R]
	bool airslowmo_ground_immediate_end = false;     // [R]
	bool airslowmo_when_hovering = true;             // [R]

	// @group ConVarsGrapple
	float grapple_shootVel = 3000.f;                 // [R]
	float grapple_retractVel = 6000.f;               // [R]
	float grapple_forcedRetractVel = 3000.f;         // [R]
	float grapple_fallSpeed = 300.f;                 // [R]
	float grapple_lift = 25.f;                       // [R]
	float grapple_pullDelay_human = 0.2f;            // [R]
	float grapple_initialImpulse_human = 350.f;      // [R] capped by speedRampMin as decompiled
	float grapple_initialImpulseOffGround_human = 50.f; // [R]
	float grapple_initialSpeedMin_human = 0.f;       // [R]
	float grapple_initialSlowFrac_human = 1.f;       // [R]
	float grapple_initialSlowFracVert_human = 0.4f;  // [R]
	float grapple_accel_human = 1000.f;              // [R]
	float grapple_decel_human = 425.f;               // [R]
	float grapple_speedRampMin_human = 50.f;         // [R]
	float grapple_speedRampMax_human = 800.f;        // [R]
	float grapple_speedRampTime_human = 1.5f;        // [R]
	bool grapple_dontFightGravity = true;            // [R]
	float grapple_letGravityHelpCosAngle = 0.8f;     // [R]
	float grapple_gravityPushUnderContribution = 2.f; // [R]
	float grapple_around_obstacle_accel = 1000.f;    // [R] applied x4
	float grapple_gracePeriod = 0.25f;               // [R]
	float grapple_windowCheckDist = 150.f;           // [R] (window hints not implemented)

	// @group ConVarsZipline
	float zipline_use_range = 120.f;                 // [R]
};

// ---------------------------------------------------------------------------------------------
// Input (one movement command)
// ---------------------------------------------------------------------------------------------
struct TFPilotInput
{
	float ForwardMove = 0.f;        // -1..1 (normalised, like mv.forwardmove)
	float SideMove = 0.f;           // -1..1, positive = right
	float ViewPitch = 0.f;          // degrees, Source convention (positive = looking down)
	float ViewYaw = 0.f;            // degrees, Source convention (counter-clockwise from +X)

	bool bJumpPressed = false;      // pressed this command
	bool bJumpHeld = false;
	bool bCrouchPressed = false;
	bool bCrouchHeld = false;
	bool bSprintPressed = false;
	bool bSprintHeld = false;
	bool bAdsPressed = false;
	bool bAdsHeld = false;
	bool bGrapplePressed = false;   // fire / release the grapple
	bool bGrappleHeld = false;
	bool bUsePressed = false;       // zipline attach [I]

	float WeaponSpeedScale = 1.f;   // WeaponSpeedScale (weapon data)
	float AdsMoveSpeedScale = 1.f;  // weapon f1484, ADS speed scale [I]
	bool bAlive = true;
};

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------
enum class ETFDuckState : uint8_t { Standing = 0, Ducking = 1, Ducked = 2, Unducking = 3 }; // [I] values

enum class ETFMantleClass : uint8_t { None = 0, Below = 3, Level = 4, Above = 5, High = 6, Idle = 11, JumpOff = 12 };

enum class ETFHookPhase : uint8_t { None, Flying, Attached, Retracting };

struct TFGrappleState
{
	bool bActive = false;            // m_grappleActive: the hook is out
	ETFHookPhase Phase = ETFHookPhase::None;
	TFVec3 HookVel;                  // m_grappleVel
	TFVec3 Points[4];                // [0] = anchor nearest the player / pull target, [Count-1] = tip
	int PointCount = 0;
	bool bAttached = false;
	bool bPulling = false;
	bool bRetracting = false;        // returning without a hit (can still attach)
	bool bForcedRetracting = false;
	float UsedPower = 0.f;
	float PullTime = 0.f;
	float AttachTime = 0.f;
	float DetachTime = 0.f;
	bool bHasGoodVelocity = true;
	float LastGoodVelocityTime = 0.f;
	int64_t HitEntityId = 0;
};

struct TFZiplineState
{
	int State = 0;                   // m_ziplineState: 0 none, 1 mount start, 2 mount blend, 3 ride
	int RopeId = -1;
	int Segment = 0;                 // segment index: nodes[Segment] -> nodes[Segment+1]
	bool bReverse = false;           // m_ziplineReverse
	int LastRopeId = -1;             // m_lastZipline
	float LastCooldown = 0.f;        // m_useLastZiplineCooldown
	float MountDuration = 0.5f;      // m_mountingZiplineDuration
	float MountAlpha = 0.f;          // m_mountingZiplineAlpha
	TFVec3 MountStartPos;
	TFVec3 MountEntryVel;
	TFVec3 AttachPoint;              // rope point (on the rope, not lowered)
	float DetachTime = 0.f;
};

struct TFMantleState
{
	bool bActive = false;
	ETFMantleClass BaseClass = ETFMantleClass::None;
	ETFMantleClass AnimClass = ETFMantleClass::None;
	bool bCrouched = false;
	TFVec3 Begin, Mid, End;
	TFVec3 ForwardDir;               // m_traversalForwardDir (= -wall normal)
	TFVec3 WallNormal;
	float MidFrac = 0.5f;
	float Progress = 0.f;
	float StartTime = -100.f;        // m_traversalStartTime
	float ReleaseTime = -100.f;      // m_traversalReleaseTime
	float JumpOffTime = 0.f;         // m_wallDangleJumpOffTime
	bool bMayHang = false;
	bool bForceFallOff = false;
	bool bLastPushedForward = false;
	bool bWeaponDisabled = false;
	float ClimbProgressFloor = 0.f;
	bool bFirstTick = false;
};

struct TFPilotState
{
	// Kinematics
	TFVec3 Origin;                   // feet
	TFVec3 Velocity;
	TFVec3 BaseVelocity;

	// Ground / surface
	bool bOnGround = false;
	TFVec3 GroundNormal = TFVec3(0.f, 0.f, 1.f);
	TFVec3 UpDir = TFVec3(0.f, 0.f, 1.f);   // m_upDir: (0,0,1) on floors, the wall normal while wallrunning
	TFVec3 UpDirWhenLastTouchedGround = TFVec3(0.f, 0.f, 1.f);
	TFVec3 LastWallNormal;          // m_lastWallNormal
	float SurfaceFriction = 1.f;     // m_surfaceFriction
	float TimeLastTouchedGround = -100.f;
	float TimeLastJumped = -100.f;
	float TimeLastLanded = -100.f;
	float OnSlopeTime = 0.f;         // m_onSlopeTime (read as last-ground time, see W1)
	bool bHasJumpedSinceTouchedGround = false;
	float FallVelocity = 0.f;
	float DamageImpulseNoDecelEndTime = 0.f;
	float PushAwayFromTopAccel = 0.f;
	TFVec3 PushAwayFromTopDir;

	// Stance
	ETFDuckState DuckState = ETFDuckState::Standing;
	float DuckTimer = 0.f;           // remaining seconds of the current transition
	float DuckTransitionLength = 0.f;
	bool bCrouchedHull = false;
	bool bDuckToggle = false;        // duck-toggle latch (0x15c8)
	int CrouchMode = 0;              // 0x15cc: 1 = no crouch, 2 = forced, 3 = forced slide

	// Sprint
	bool bSprinting = false;
	bool bSprintToggleLatch = false;
	float SprintStartedTime = -100.f;
	float SprintEndedTime = -100.f;
	float SprintTilt = 0.f;          // -1..1
	float SprintTiltVel = 0.f;
	float PrevMoveYaw = 0.f;

	// Jump
	int AirJumpCount = 0;
	float SuperjumpPower = 100.f;

	// Slide
	bool bSliding = false;
	bool bSlideLongJumpAllowed = false;
	float LastSlideTime = -100.f;
	float LastSlideBoost = 0.f;

	// Wallrun
	bool bWallRunning = false;
	TFVec3 WallNormal;
	TFVec3 LastWallRunStartPos;
	float WallRunStartTime = -100.f;
	int WallRunCount = 0;
	bool bWallRunWeak = true;
	float WallRunPushAwayTime = 0.f;
	float WallrunRetryTime = 0.f;
	TFVec3 WallrunRetryPos;
	TFVec3 WallrunRetryNormal;
	bool bWallHanging = false;
	float WallHangStartTime = 0.f;
	float WallHangElapsed = 0.f;
	bool bAdsLatch = false;          // 0x1599 ADS-held latch
	TFVec3 RepelNormal;
	float RepelTime = -100.f;
	int RepelCount = 0;
	float ViewRoll = 0.f;            // [I] client-side wallrun roll (degrees, Source sign)

	// Mantle
	TFMantleState Mantle;

	// Dodge
	bool bDodgingInAir = false;
	bool bDodging = false;
	float LastDodgeTime = -100.f;

	// Hover / slow-mo
	bool bHovering = false;
	float GlideMeter = 1.f;
	float AirSlowMoBlend = 0.f;

	// Grapple / zipline
	TFGrappleState Grapple;
	TFZiplineState Zipline;

	// Power
	float SuitPower = 100.f;
	float GrapplePower = 100.f;
	float TimeAllSuitDevicesOff = 0.f;
	float LastGrapplePowerUse = -100.f;

	// Status effects
	float SpeedBoostSeverity = 0.f;  // speed_boost (Stim). SpeedScale x (1 + 2*severity)
	float SpeedBoostEndTime = 0.f;
	float SpeedBoostFadeTime = 0.f;
	float SpeedBoostBaseSeverity = 0.f;
	float MoveSlowSeverity = 0.f;    // move_slow. SpeedScale x (1 - severity)

	// ADS
	float AdsFraction = 0.f;

	// Bookkeeping
	uint32_t TickCount = 0;          // command number (probe phases)
	float CurTime = 0.f;
	float MaxSpeed = 0.f;            // mv.maxspeed of the last tick
	float EyeHeight = 60.f;          // current view height (blends with duck)
	float LastImpactStrength = 0.f;
	float PrevForwardMove = 0.f;
	float PrevSideMove = 0.f;
	bool bNeedStuckCheck = false;
};

// ---------------------------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------------------------

// Per-tick entry point (doc sec.1 frame order). Uses default ConVars.
void TFPilotMove(TFPilotState& State, const TFPilotInput& Input, const TFPilotSettings& Settings, ITFWorld& World, float Dt);

// Same, with explicit ConVars.
void TFPilotMoveEx(TFPilotState& State, const TFPilotInput& Input, const TFPilotSettings& Settings,
	const TFMovementConVars& ConVars, ITFWorld& World, float Dt);

// Starts Stim (speed_boost severity for Duration + StimExtraTime seconds, doc sec.13).
void TFStartStim(TFPilotState& State, const TFPilotSettings& Settings, float Now, float Duration);

// Places the pilot (resets transient state).
void TFResetPilot(TFPilotState& State, const TFVec3& FeetOrigin);

// Queries
inline bool TFIsWallRunning(const TFPilotState& S) { return S.bWallRunning; }
inline bool TFIsMantling(const TFPilotState& S) { return S.Mantle.bActive; }
inline bool TFIsZiplining(const TFPilotState& S) { return S.Zipline.State != 0; }
inline bool TFIsGrappling(const TFPilotState& S) { return S.Grapple.bActive && S.Grapple.bAttached; }
float TFGetHullHeight(const TFPilotState& S, const TFPilotSettings& Settings);
// Effective (stimmed, slowed) speed scale: weapon x moveSpeedScale x (1 - slow) x (1 + 2*boost).
float TFGetSpeedScale(const TFPilotState& S, const TFPilotInput& In, const TFPilotSettings& Settings);
