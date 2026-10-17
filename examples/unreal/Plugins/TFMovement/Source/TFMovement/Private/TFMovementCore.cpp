// TFMovementCore.cpp
//
// Clean-room implementation of Titanfall 2 pilot movement from the docs in docs/.
// Section references ("doc sec.N") point to docs/TITANFALL2_MOVEMENT.md unless another note is named.
// Plain C++17, no engine headers.

#include "TFMovementCore.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

// Internal helpers live in a named namespace (not anonymous) so unity builds cannot collide.
namespace TFMoveCoreImpl
{
// -------------------------------------------------------------------------------------------
// Constants (hard-coded in the game code, all [R] unless noted)
// -------------------------------------------------------------------------------------------
constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.f;
constexpr float kStepClearance = 1.f / 32.f;   // step trace clearance
constexpr float kStepGain = 0.999f;            // stepping must gain > 0.1% horizontal distance
constexpr float kStepSlopeRatio = 0.49f;       // ~45.6 deg acceptance for steep landings
constexpr float kAirProbeDist = 2.f;           // categorize probe distance in the air
constexpr float kImpactPartialStrength = 0.85f;
constexpr float kGrappleObstacleTrigger = -0.5f;
constexpr float kGrappleObstacleAccelMul = 4.f;
constexpr float kGrappleLeaveGroundDot = -0.3f;
constexpr float kGrappleLeaveWallDot = -0.001f;
constexpr float kGrappleCornerRadiusSq = 900.f; // 30 u squared
constexpr int kMaxGrapplePoints = 4;
constexpr float kMantleProbeHeights[5] = { 0.f /*= top*/, 66.5f, 34.5f, 26.5f, 16.5f };
constexpr float kMantleSettleStep = 4.f;

// -------------------------------------------------------------------------------------------
// Scalar helpers
// -------------------------------------------------------------------------------------------
inline float Sq(float V) { return V * V; }
inline float Clamp(float V, float Lo, float Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
inline float Clamp01(float V) { return Clamp(V, 0.f, 1.f); }
inline float LerpF(float A, float B, float T) { return A + (B - A) * T; }
inline float Approach(float Cur, float Target, float Step)
{
	if (Cur < Target) { return std::min(Cur + Step, Target); }
	return std::max(Cur - Step, Target);
}
inline bool IsBad(float V) { return V != V || std::fabs(V) > 1e30f; }

// -------------------------------------------------------------------------------------------
// Per-tick context
// -------------------------------------------------------------------------------------------
struct TFMoveCtx
{
	TFPilotState& S;
	const TFPilotInput& In;
	const TFPilotSettings& P;
	const TFMovementConVars& CV;
	ITFWorld& W;
	float Dt = 0.f;       // frametime
	float MoveDt = 0.f;   // frametime x air slow-mo scale (FullWalkMove dt)
	float Now = 0.f;

	TFVec3 ViewFwd, ViewRight, ViewUp;
	TFVec3 YawFwd, YawRight;
	TFVec3 WishDir;       // mv.wishdir: world, horizontal, magnitude 0..1
	float ForwardMove = 0.f;
	float SideMove = 0.f;

	bool bJumpPressed = false;
	bool bFwdPressed = false, bBackPressed = false, bLeftPressed = false, bRightPressed = false;
	bool bFwdHeld = false, bBackHeld = false, bLeftHeld = false, bRightHeld = false;

	TFMoveCtx(TFPilotState& InS, const TFPilotInput& InIn, const TFPilotSettings& InP,
		const TFMovementConVars& InCV, ITFWorld& InW)
		: S(InS), In(InIn), P(InP), CV(InCV), W(InW) {}
};

struct TFHitRecord
{
	TFVec3 EndPos;
	TFVec3 Normal;
	TFVec3 VelBeforeClip;
};

struct TFMoveHits
{
	TFHitRecord Hits[8];
	int Num = 0;
	void Add(const TFVec3& Pos, const TFVec3& N, const TFVec3& V)
	{
		if (Num < 8) { Hits[Num].EndPos = Pos; Hits[Num].Normal = N; Hits[Num].VelBeforeClip = V; ++Num; }
	}
};

// -------------------------------------------------------------------------------------------
// Forward declarations
// -------------------------------------------------------------------------------------------
void SetGround(TFMoveCtx& C, bool bOnGround, const TFVec3& Normal);
void EndSlide(TFMoveCtx& C);
void TryStartSlide(TFMoveCtx& C, const TFVec3& GroundNormal);
void StopWallRun(TFMoveCtx& C);
void EndWallRun(TFMoveCtx& C, bool bApplyFallAway, bool bTimedOut);
void TryStartWallRun(TFMoveCtx& C, const TFVec3& HitPos, const TFVec3& N, const TFVec3& Vel);
bool MantleAngleOK(TFMoveCtx& C, const TFVec3& WallN);
void GrappleDetach(TFMoveCtx& C, bool bApplyEffects);
void ZiplineDetach(TFMoveCtx& C);
bool CheckJumpButton(TFMoveCtx& C, bool bForcedGroundJump);
void FinishGravity(TFMoveCtx& C, float Dt);
void CheckVelocity(TFMoveCtx& C);

// -------------------------------------------------------------------------------------------
// Basic queries
// -------------------------------------------------------------------------------------------
float HullHeight(const TFMoveCtx& C)
{
	return C.S.bCrouchedHull ? C.P.CrouchHullHeight : C.P.StandHullHeight;
}

TFVec3 EyePos(const TFMoveCtx& C)
{
	return C.S.Origin + TFVec3(0.f, 0.f, C.S.EyeHeight);
}

TFTraceResult Sweep(TFMoveCtx& C, const TFVec3& Start, const TFVec3& End)
{
	return C.W.SweepHull(Start, End, C.S.bCrouchedHull);
}

void Event(TFMoveCtx& C, ETFMoveEvent E, float Value = 0.f)
{
	C.W.OnMoveEvent(E, Value);
}

// SpeedScale (ground_misc.md sec.2.1): weapon x m_playerMoveSpeedScale x (1 - slow) x (1 + 2*boost).
// The status-effect identities (move_slow / speed_boost) are [I] (ASSUMPTIONS G1).
// Denuvo_SlowSpeed (x0.1 after 600 s on tampered copies) is intentionally omitted.
float SpeedScale(const TFMoveCtx& C)
{
	return C.In.WeaponSpeedScale * C.P.MoveSpeedScale
		* (1.f - C.S.MoveSlowSeverity)
		* (1.f + C.P.SpeedBoostFactor * C.S.SpeedBoostSeverity);
}

// MaxSpeedScale: ADS blend, not applied while wallrunning (ground_misc.md sec.2.1). [I] identities (G7).
float MaxSpeedScale(const TFMoveCtx& C)
{
	if (C.S.bWallRunning) { return 1.f; }
	return LerpF(1.f, C.In.AdsMoveSpeedScale, C.S.AdsFraction);
}

// Stance 0 = standing, 1 = crouched [I] (ASSUMPTIONS G2).
float StanceSpeed(const TFMoveCtx& C, int Stance)
{
	return SpeedScale(C) * (Stance == 0 ? C.P.WalkSpeed : C.P.CrouchSpeed);
}

float StanceSprintSpeed(const TFMoveCtx& C, int Stance)
{
	return SpeedScale(C) * (Stance == 0 ? C.P.SprintSpeed : C.P.CrouchSprintSpeed);
}

int CurrentStance(const TFMoveCtx& C) { return C.S.bCrouchedHull ? 1 : 0; }

// BaseMaxSpeed: always stance 0, sprint or walk.
float BaseMaxSpeed(const TFMoveCtx& C)
{
	return SpeedScale(C) * (C.S.bSprinting ? C.P.SprintSpeed : C.P.WalkSpeed);
}

float WallrunTimeLimit(const TFMoveCtx& C)
{
	return C.P.WallrunTimeLimit + (C.P.bWallRunnerKit ? C.P.WallRunnerKitTimeAdd : 0.f);
}

float WallrunHangTimeLimit(const TFMoveCtx& C)
{
	return C.P.WallrunHangTimeLimit + (C.P.bWallRunnerKit ? C.P.WallRunnerKitHangTimeAdd : 0.f);
}

bool IsDuckHeld(const TFMoveCtx& C) { return C.In.bCrouchHeld || C.S.bDuckToggle; }

// -------------------------------------------------------------------------------------------
// Velocity clamp (ground_misc.md sec.2.7)
// -------------------------------------------------------------------------------------------
void CheckVelocity(TFMoveCtx& C)
{
	TFVec3& V = C.S.Velocity;
	if (IsBad(V.X)) { V.X = 0.f; }
	if (IsBad(V.Y)) { V.Y = 0.f; }
	if (IsBad(V.Z)) { V.Z = 0.f; }
	TFVec3& O = C.S.Origin;
	if (IsBad(O.X)) { O.X = 0.f; }
	if (IsBad(O.Y)) { O.Y = 0.f; }
	if (IsBad(O.Z)) { O.Z = 0.f; }
	O.X = Clamp(O.X, -C.CV.MapBound, C.CV.MapBound);
	O.Y = Clamp(O.Y, -C.CV.MapBound, C.CV.MapBound);
	O.Z = Clamp(O.Z, -C.CV.MapBound, C.CV.MapBound);

	// Whole vector scaled so no axis exceeds sv_maxvelocity (unlike stock per-axis clamp).
	const float MaxAxis = std::max(std::fabs(V.X), std::max(std::fabs(V.Y), std::fabs(V.Z)));
	if (MaxAxis > C.CV.sv_maxvelocity)
	{
		V *= C.CV.sv_maxvelocity / MaxAxis;
	}
}

// -------------------------------------------------------------------------------------------
// Collision move (TryPlayerMove-style multi-bump slide)
// -------------------------------------------------------------------------------------------
void ClipVelocity(const TFVec3& In, const TFVec3& N, TFVec3& Out)
{
	const float Backoff = In.Dot(N);
	Out = In - N * Backoff;
	const float Adjust = Out.Dot(N);
	if (Adjust < 0.f) { Out -= N * Adjust; }
}

// Moves Pos by Vel*Dt with clipping against up to 4 bumps. Returns true when anything was hit.
bool SlideMove(TFMoveCtx& C, TFVec3& Pos, TFVec3& Vel, float Dt, TFMoveHits* OutHits)
{
	TFVec3 Planes[5];
	int NumPlanes = 0;
	const TFVec3 Primal = Vel;
	TFVec3 Original = Vel;
	float TimeLeft = Dt;
	bool bHitAny = false;
	bool bNudged = false;

	for (int Bump = 0; Bump < 4; ++Bump)
	{
		if (Vel.IsNearlyZero(1e-4f)) { break; }
		const TFVec3 End = Pos + Vel * TimeLeft;
		TFTraceResult Tr = Sweep(C, Pos, End);
		if (Tr.bStartSolid)
		{
			// [I] simplified unstick: nudge once along the reported normal, else stop.
			if (!bNudged && !Tr.Normal.IsNearlyZero())
			{
				Pos += Tr.Normal * 0.25f;
				bNudged = true;
				--Bump;
				continue;
			}
			C.S.bNeedStuckCheck = true;
			Vel = TFVec3();
			return true;
		}
		if (Tr.Fraction > 0.f)
		{
			Pos = Tr.EndPos;
			NumPlanes = 0;
			Original = Vel;
		}
		if (Tr.Fraction >= 1.f) { break; }

		bHitAny = true;
		if (OutHits) { OutHits->Add(Tr.EndPos, Tr.Normal, Vel); }
		TimeLeft -= TimeLeft * Tr.Fraction;

		if (NumPlanes >= 5) { Vel = TFVec3(); break; }
		Planes[NumPlanes++] = Tr.Normal;

		int I = 0;
		TFVec3 NewVel;
		for (I = 0; I < NumPlanes; ++I)
		{
			ClipVelocity(Original, Planes[I], NewVel);
			int J = 0;
			for (J = 0; J < NumPlanes; ++J)
			{
				if (J != I && NewVel.Dot(Planes[J]) < 0.f) { break; }
			}
			if (J == NumPlanes) { break; }
		}
		if (I != NumPlanes)
		{
			Vel = NewVel;
		}
		else
		{
			if (NumPlanes != 2) { Vel = TFVec3(); break; }
			const TFVec3 Dir = Planes[0].Cross(Planes[1]).Normalized();
			Vel = Dir * Dir.Dot(Vel);
		}
		if (Vel.Dot(Primal) <= 0.f) { Vel = TFVec3(); break; }
	}
	return bHitAny;
}

// -------------------------------------------------------------------------------------------
// Stance / duck (ground_misc.md sec.2.6)
// -------------------------------------------------------------------------------------------
bool CanUnduck(TFMoveCtx& C)
{
	TFVec3 Pos = C.S.Origin;
	if (!C.S.bOnGround && !C.S.bWallRunning)
	{
		// [I] stock Source: in the air the legs were tucked, so standing extends them downward.
		Pos.Z -= (C.P.StandHullHeight - C.P.CrouchHullHeight);
	}
	const TFTraceResult Tr = C.W.SweepHull(Pos, Pos, false);
	return !Tr.bStartSolid;
}

void FinishDuck(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (!S.bCrouchedHull && !S.bOnGround && !S.bWallRunning)
	{
		// [I] stock Source behaviour: ducking in the air tucks the legs (feet move up).
		S.Origin.Z += (C.P.StandHullHeight - C.P.CrouchHullHeight);
	}
	S.bCrouchedHull = true;
	S.DuckState = ETFDuckState::Ducked;
	S.DuckTimer = 0.f;
}

void FinishUnDuck(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (S.bCrouchedHull && !S.bOnGround && !S.bWallRunning)
	{
		S.Origin.Z -= (C.P.StandHullHeight - C.P.CrouchHullHeight);
	}
	S.bCrouchedHull = false;
	S.DuckState = ETFDuckState::Standing;
	S.DuckTimer = 0.f;
	EndSlide(C); // the fully-standing branch calls EndSlide
}

void ForceDuck(TFMoveCtx& C)
{
	C.S.bCrouchedHull = true;
	C.S.DuckState = ETFDuckState::Ducked;
	C.S.DuckTimer = 0.f;
}

void BeginDucking(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const float Len = S.bSliding ? C.P.DuckTimeSliding : C.P.DuckTime;
	S.DuckState = ETFDuckState::Ducking;
	S.DuckTransitionLength = Len;
	S.DuckTimer = Len;
}

void Duck(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;

	bool bWants = IsDuckHeld(C);
	// Forced duck while sliding faster than slideMaxStopSpeed [R].
	if (S.bSliding && S.Velocity.Length() > P.SlideMaxStopSpeed) { bWants = true; }
	if (S.CrouchMode == 2 || S.CrouchMode == 3) { bWants = true; }
	// Blocks [R]: crouch mode 1, ziplining, wallrunning, class crouch off.
	const bool bBlocked = S.CrouchMode == 1 || S.Zipline.State != 0 || S.bWallRunning || !P.bCrouchEnabled || S.Mantle.bActive;
	if (bBlocked) { bWants = false; }
	if (S.Mantle.bActive) { return; } // traversal owns the stance

	switch (S.DuckState)
	{
	case ETFDuckState::Standing:
		if (bWants)
		{
			if (S.bOnGround && !S.bSliding) { TryStartSlide(C, S.GroundNormal); } // [I] caller of TryStartSlide (S1)
			if (S.DuckState == ETFDuckState::Standing) { BeginDucking(C); }
		}
		break;
	case ETFDuckState::Ducking:
		if (!bWants)
		{
			// Reversal rescales the timer by the fraction already done [R].
			const float Done = (S.DuckTransitionLength > 0.f) ? 1.f - S.DuckTimer / S.DuckTransitionLength : 1.f;
			S.DuckState = ETFDuckState::Unducking;
			S.DuckTransitionLength = P.UnduckTime;
			S.DuckTimer = P.UnduckTime * Done;
		}
		break;
	case ETFDuckState::Ducked:
		if (!bWants && CanUnduck(C))
		{
			S.DuckState = ETFDuckState::Unducking;
			S.DuckTransitionLength = P.UnduckTime;
			S.DuckTimer = P.UnduckTime;
		}
		break;
	case ETFDuckState::Unducking:
		if (bWants)
		{
			const float Done = (S.DuckTransitionLength > 0.f) ? 1.f - S.DuckTimer / S.DuckTransitionLength : 1.f;
			const float Len = S.bSliding ? P.DuckTimeSliding : P.DuckTime;
			S.DuckState = ETFDuckState::Ducking;
			S.DuckTransitionLength = Len;
			S.DuckTimer = Len * Done;
		}
		break;
	}

	// ReduceTimers + finish transitions.
	if (S.DuckState == ETFDuckState::Ducking || S.DuckState == ETFDuckState::Unducking)
	{
		S.DuckTimer = std::max(0.f, S.DuckTimer - C.Dt);
		if (S.DuckTimer <= 0.f)
		{
			if (S.DuckState == ETFDuckState::Ducking)
			{
				FinishDuck(C);
			}
			else if (!S.bCrouchedHull || CanUnduck(C))
			{
				FinishUnDuck(C);
			}
		}
	}
}

float DuckFraction(const TFPilotState& S)
{
	switch (S.DuckState)
	{
	case ETFDuckState::Standing: return 0.f;
	case ETFDuckState::Ducked: return 1.f;
	case ETFDuckState::Ducking:
		return S.DuckTransitionLength > 0.f ? 1.f - S.DuckTimer / S.DuckTransitionLength : 1.f;
	case ETFDuckState::Unducking:
		return S.DuckTransitionLength > 0.f ? S.DuckTimer / S.DuckTransitionLength : 0.f;
	}
	return 0.f;
}

// -------------------------------------------------------------------------------------------
// Ground state (jump_air_friction.md SetGroundEntity / ReplenishAirAbilities)
// -------------------------------------------------------------------------------------------
void ReplenishAirAbilities(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	S.bHasJumpedSinceTouchedGround = false;
	S.WallrunRetryTime = 0.f;
	if (S.UpDir.Z < 1.f)
	{
		// Wall contact.
		if (!C.P.bSuperjumpCanUseAfterWallrun) { S.AirJumpCount = static_cast<int>(C.P.SuperjumpLimit); }
		else if (C.CV.superjump_limitreset_onwallrun && S.AirJumpCount != 0) { S.AirJumpCount = 0; }
	}
	else
	{
		// Real ground: forget remembered walls, refresh double jump, next wallrun is weak.
		S.RepelCount = 0;
		S.AirJumpCount = 0;
		S.bWallRunWeak = true;
		if (C.CV.superjump_powerreset_onground) { S.SuperjumpPower = 100.f; }
	}
}

void SetGround(TFMoveCtx& C, bool bNewGround, const TFVec3& Normal)
{
	TFPilotState& S = C.S;
	if (!bNewGround)
	{
		S.bOnGround = false;
		return;
	}
	const bool bWasGround = S.bOnGround;
	S.bOnGround = true;
	S.GroundNormal = Normal;
	if (!bWasGround)
	{
		S.TimeLastLanded = C.Now; // skip timer
		if (S.FallVelocity >= C.P.LandingFxFallSpeed) { Event(C, ETFMoveEvent::Land, S.FallVelocity); }
		if (S.DuckState == ETFDuckState::Ducking || S.DuckState == ETFDuckState::Ducked)
		{
			TryStartSlide(C, Normal); // slide start on a crouched landing [R]
		}
	}
	if (C.CV.skip_replenish_double_jump || (C.Now - S.TimeLastLanded > C.CV.skip_time) || S.bWallRunning)
	{
		ReplenishAirAbilities(C);
	}
	S.Velocity -= S.UpDir * S.Velocity.Dot(S.UpDir);
}

// -------------------------------------------------------------------------------------------
// Wallrun helpers (wallrun.md)
// -------------------------------------------------------------------------------------------
float WallRunStrength(const TFMoveCtx& C)
{
	const TFPilotState& S = C.S;
	if (S.bWallRunWeak) { return 0.f; }
	const float Count = static_cast<float>(S.WallRunCount);
	const float A = C.CV.wallrun_strengthLossStart;
	const float B = C.CV.wallrun_strengthLossEnd;
	if (Count <= A) { return 1.f; }
	if (Count < B) { return (Count - A) / (B - A); } // rises 0->1 as written in the code
	return 0.f;
}

float WallHangFactor(const TFMoveCtx& C)
{
	const TFPilotState& S = C.S;
	if (!S.bWallRunning || !S.bWallHanging) { return 1.f; }
	const float H = S.WallHangElapsed;
	const float Slip = Clamp01((H - C.CV.wallrun_hangslipstarttime) / std::max(C.CV.wallrun_hangslipduration, 1e-4f));
	if (H >= C.CV.wallrun_hangStopTime) { return Slip; }
	const float A = 1.f - H / std::max(C.CV.wallrun_hangStopTime, 1e-4f);
	return (1.f - A) * Slip + A;
}

// Gravity ramp 0 -> full over wallrun_gravityRampUpTime on a strong run. Applied only while
// wallrunning [I]: the doc does not say whether the ramp continues after a jump-off.
float WallRunGravityScale(const TFMoveCtx& C)
{
	const TFPilotState& S = C.S;
	if (!S.bWallRunning) { return 1.f; }
	const float T = C.P.bWallstickEnabled ? 100.f : C.CV.wallrun_gravityRampUpTime;
	const float Elapsed = std::max(C.Now - S.WallRunStartTime, 0.f);
	const float H = WallHangFactor(C);
	if (Elapsed < T) { return H * (1.f - (1.f - Elapsed / T) * WallRunStrength(C)); }
	return H;
}

// Rate-limited rotation of the stored wall normal (RotateWallNormal). Rate in rad/s [I].
void RotateWallNormal(TFMoveCtx& C, const TFVec3& NewN)
{
	TFPilotState& S = C.S;
	const float Ang = C.CV.wallrun_rotateMaxRate * C.Dt;
	TFVec3 N = S.WallNormal;
	if (NewN.Dot(N) > std::cos(Ang))
	{
		N = NewN;
	}
	else
	{
		const TFVec3 Perp = (NewN - N * N.Dot(NewN)).Normalized();
		N = (N * std::cos(Ang) + Perp * std::sin(Ang)).Normalized();
	}
	if (!N.IsNearlyZero())
	{
		S.WallNormal = N;
		S.UpDir = N;
	}
}

// WallProbe: returns 2 = wall found, 1 = near miss (retry), 0 = fail.
// The real helper was not decompiled (ASSUMPTIONS W3); this line probe is [I].
int WallProbe(TFMoveCtx& C, const TFVec3& Base, const TFVec3& N, float AllowedDist, TFTraceResult* OutTr)
{
	const float Reach = C.P.HullRadius + AllowedDist;
	const TFTraceResult Tr = C.W.TraceLine(Base, Base - N * Reach);
	if (OutTr) { *OutTr = Tr; }
	if (Tr.Hit() && !Tr.bStartSolid)
	{
		const float Nz = Tr.Normal.Z;
		return (Nz >= C.P.WallrunMinNormalZ && Nz <= C.P.WallrunMaxNormalZ) ? 2 : 0;
	}
	const TFTraceResult Far = C.W.TraceLine(Base, Base - N * (Reach + AllowedDist));
	return Far.Hit() ? 1 : 0;
}

void RecordRepelWall(TFMoveCtx& C, const TFVec3& N)
{
	if (!C.CV.wallrun_repelEnable) { return; }
	C.S.RepelNormal = N.Normalized2D();
	C.S.RepelTime = C.Now;
	C.S.RepelCount = 1;
}

// ApplyWallRepel has no caller among the analysed functions [?] (W6). Off by default.
void ApplyWallRepel(TFMoveCtx& C, TFVec3& Vel, const TFVec3& Dir)
{
	const TFPilotState& S = C.S;
	if (S.RepelCount == 0) { return; }
	float T = C.Now - S.RepelTime;
	if (S.bWallRunning) { T *= 0.3f; }
	const float Softness = C.CV.wallrun_repelSoftness;
	const float Soft = T < 0.2f ? 0.f : (T < 0.5f ? Softness * (T - 0.2f) / 0.3f : Softness);
	float Wt = 1.f;
	if (T > C.CV.wallrun_repelTimeMin)
	{
		if (T >= C.CV.wallrun_repelTimeMax) { return; }
		Wt = (T - C.CV.wallrun_repelTimeMax) / (C.CV.wallrun_repelTimeMin - C.CV.wallrun_repelTimeMax);
	}
	TFVec3 V = Vel;
	const TFVec3& Pn = S.RepelNormal;
	const float Into = -Pn.Dot(V);
	if (Into > 0.f)
	{
		const float Allow = Pn.Dot(Dir) > 0.f ? Pn.Dot(Dir) * Soft : 0.f;
		if (Allow < Into) { V += Pn * (Into - Allow); }
	}
	Vel = TFVec3::Lerp(Vel, V, Wt);
}

// CanWallRun (wallrun.md sec.2). Returns 0 / 1 / 2.
int CanWallRun(TFMoveCtx& C, const TFVec3& HitPos, const TFVec3& N, const TFVec3& Vel)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	const TFMovementConVars& CV = C.CV;

	if (!CV.wallrun_enable || !P.bWallrunEnabled) { return 0; }
	// [I] W1: m_onSlopeTime read as "last time on the ground"; a jump zeroes it.
	if ((S.bOnGround || C.Now - S.OnSlopeTime < P.WallrunMinStartAirTime) && !S.bWallRunning) { return 0; }
	// [I] W2: FUN_1805e6af0 is most likely "on zipline"; dodging also blocks.
	if (S.Zipline.State != 0 || S.bDodgingInAir) { return 0; }
	if (N.Z > P.WallrunMaxNormalZ || N.Z < P.WallrunMinNormalZ) { return 0; }
	if (S.Grapple.bActive && S.Grapple.PointCount > 0)
	{
		const TFVec3 GDir = (S.Grapple.Points[0] - EyePos(C)).Normalized();
		if (GDir.Dot(N) > 0.f) { return 0; }
	}
	if (S.DuckState != ETFDuckState::Standing && !CanUnduck(C) && S.CrouchMode != 3) { return 0; }

	// Same-wall rule.
	if (!P.bWallstickEnabled && S.RepelCount > 0)
	{
		const TFVec3 Nh = N.Normalized2D();
		if (Nh.Dot(S.RepelNormal) > CV.wallrun_sameWallDot)
		{
			if (!P.bWallrunSameWallAllowed) { return 0; }
			const float ZRef = S.LastWallRunStartPos.Z + CV.wallrun_sameWallHeight;
			if (S.Origin.Z >= ZRef)
			{
				const float D = (S.Origin - S.LastWallRunStartPos).Length2D();
				if (D <= CV.wallrun_sameWallDist) { return 0; }
				if ((D - CV.wallrun_sameWallDist) * CV.wallrun_sameWallSlope <= S.Origin.Z - ZRef) { return 0; }
			}
		}
	}

	// Approach angle, only when starting.
	if (!S.bWallRunning)
	{
		const TFVec3 V = Vel.Normalized();
		if (V.Dot(N) >= -std::cos(CV.wallrun_minAngle_air * 0.5f * kDegToRad)) { return 0; }
	}

	TFVec3 Base = HitPos;
	const bool bBehind = (S.Origin - HitPos).Dot(N) < -0.01f;
	if (bBehind) { Base = S.Origin; }

	const float Offsets[2] = { P.WallrunProbeLowOffset, HullHeight(C) * 0.5f + P.HullRadius };
	for (float Off : Offsets)
	{
		TFTraceResult Tr;
		const int R = WallProbe(C, Base + TFVec3(0.f, 0.f, Off), N, CV.wallrun_allowed_wall_dist, &Tr);
		if (R != 2) { return R; }
		if (bBehind && Tr.Fraction > 0.7f) { return 0; }
	}

	// If a mantle is wanted, also need wall at the hull top, otherwise mantle instead.
	if (!S.bWallRunning && MantleAngleOK(C, N))
	{
		const int R = WallProbe(C, Base + TFVec3(0.f, 0.f, HullHeight(C) - P.HullRadius), N, CV.wallrun_allowed_wall_dist, nullptr);
		if (R != 2) { return R; }
	}

	// Denuvo_CheckWallrun anti-tamper trap intentionally omitted (doc sec.12).
	return 2;
}

void BeginWallRun(TFMoveCtx& C, const TFVec3& N)
{
	TFPilotState& S = C.S;
	S.bWallRunning = true;
	S.WallNormal = N;
	S.UpDir = N;
	S.bOnGround = false;
	S.WallRunCount++;

	// Up-along-wall boost, scaled by strength (0 on a weak run).
	const float Strength = WallRunStrength(C);
	const float Boost = C.CV.wallrun_upWallBoost * Strength;
	const float Nxy = N.Length2D();
	if (Nxy > 1e-4f)
	{
		const TFVec3 U(-N.Z * N.X / Nxy, -N.Z * N.Y / Nxy, Nxy);
		const float Vu = S.Velocity.Dot(U);
		if (Vu < Boost) { S.Velocity += U * (Boost - std::max(Vu, 0.f)); }
	}

	// Remembered same wall -> weak (after the boost, as in the code order).
	if (S.RepelCount > 0 && N.Dot(S.RepelNormal) > C.CV.wallrun_sameWallDot) { S.bWallRunWeak = true; }

	S.LastWallRunStartPos = S.Origin;
	S.WallRunStartTime = C.Now;
	S.WallRunPushAwayTime = 0.f;
	S.bWallHanging = false;
	S.WallHangElapsed = 0.f;
	S.bDodgingInAir = false;
	S.bDodging = false;
	EndSlide(C);
	ReplenishAirAbilities(C); // the wall becomes the ground entity [I]: wall branch of replenish
	S.TimeLastTouchedGround = C.Now;
	S.UpDirWhenLastTouchedGround = N;
	S.Velocity -= N * S.Velocity.Dot(N);
	Event(C, ETFMoveEvent::WallRunStart, Strength);
}

void StopWallRun(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (!S.bWallRunning) { return; }
	S.bWallRunning = false;
	S.bWallHanging = false;
	S.UpDir = TFVec3::Up();
}

// End of a wallrun from WallRunThink (crouch, push-away, time limit, wall lost, hang end).
void EndWallRun(TFMoveCtx& C, bool bApplyFallAway, bool bTimedOut)
{
	TFPilotState& S = C.S;
	if (!S.bWallRunning) { return; }
	const TFVec3 N = S.WallNormal;
	S.bAdsLatch = false;
	if (bApplyFallAway)
	{
		// The position test before the push (FUN_18016dd00) is not established [?]; we test that
		// the hull can move away from the wall.
		const TFTraceResult Tr = Sweep(C, S.Origin, S.Origin + N * 2.f);
		if (!Tr.Hit()) { S.Velocity += N * C.CV.wallrun_fallAwaySpeed; }
	}
	RecordRepelWall(C, N);
	StopWallRun(C);
	S.bWallRunWeak = true;
	SetGround(C, false, TFVec3::Up());
	Event(C, ETFMoveEvent::WallRunEnd, bTimedOut ? 1.f : 0.f);
}

void TryStartWallRun(TFMoveCtx& C, const TFVec3& HitPos, const TFVec3& N, const TFVec3& Vel)
{
	TFPilotState& S = C.S;
	S.WallrunRetryTime = 0.f;
	const int R = CanWallRun(C, HitPos, N, Vel);
	if (R == 2)
	{
		if (!S.bWallRunning) { BeginWallRun(C, N); }
		else { RotateWallNormal(C, N); }
		return;
	}
	if (R == 1 && !S.bOnGround && N.Z <= C.P.WalkableNormalZ)
	{
		S.WallrunRetryTime = C.Now + C.CV.wallrun_retry_interval;
		S.WallrunRetryPos = S.Origin;
		S.WallrunRetryNormal = N;
	}
	if (N.Z <= C.P.WalkableNormalZ)
	{
		if (S.bWallRunning && S.Velocity.Z <= C.P.WallrunLandUpSpeed)
		{
			const TFTraceResult Tr = Sweep(C, S.Origin, S.Origin - TFVec3(0.f, 0.f, C.P.StepHeight));
			if (Tr.Hit() && !Tr.bStartSolid && Tr.Normal.Z > C.P.WalkableNormalZ)
			{
				StopWallRun(C);
				SetGround(C, true, Tr.Normal);
			}
		}
	}
	else if (S.bWallRunning && (S.Origin - HitPos).Dot(N) >= 0.f)
	{
		StopWallRun(C); // hit a floor
		SetGround(C, true, N);
	}
}

// WallRunThink (wallrun.md sec.2): hang, push-away, time limit, wall probes, top-of-wall decel.
void WallRunThink(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	const TFMovementConVars& CV = C.CV;
	if (!S.bWallRunning)
	{
		return; // view-tilt prediction is client-facing only (W7) and not reproduced
	}

	const float PrevHang = S.WallHangElapsed;
	bool bEnd = C.In.bCrouchHeld; // crouch (0x4) [I bit]
	bEnd = bEnd || S.CrouchMode == 2 || S.CrouchMode == 3;
	bool bTimedOut = false;
	const TFVec3 N = S.WallNormal;

	const ETFWallrunAdsType AdsType = P.bWallHangKit ? ETFWallrunAdsType::WallHang : P.WallrunAdsType;
	const bool bAds = C.In.bAdsHeld || S.bAdsLatch;
	if (!S.bWallHanging && bAds)
	{
		if (AdsType == ETFWallrunAdsType::WallHang)
		{
			S.bWallHanging = true;
			S.WallHangStartTime = C.Now;
			S.WallHangElapsed = 0.f;
			Event(C, ETFMoveEvent::WallHangStart);
		}
		else if (AdsType == ETFWallrunAdsType::None)
		{
			S.bAdsLatch = false; // ADS is cancelled; the weapon layer should read this
		}
	}
	if (S.bWallHanging)
	{
		// [I] probe height: the hull top (mv+0x7c read as hull height).
		if (WallProbe(C, S.Origin + TFVec3(0.f, 0.f, HullHeight(C) - 1.f), N, CV.wallrun_allowed_wall_dist_wallhang, nullptr) != 2)
		{
			S.bWallHanging = false;
		}
	}
	if (!S.bWallHanging)
	{
		if (C.WishDir.Dot(N) > 0.7f)
		{
			if (S.WallRunPushAwayTime == 0.f) { S.WallRunPushAwayTime = C.Now; }
			if (C.Now - S.WallRunPushAwayTime > CV.wallrun_pushAwayFallOffTime) { bEnd = true; }
		}
		else
		{
			S.WallRunPushAwayTime = 0.f;
		}
		if (C.Now - S.WallRunStartTime > WallrunTimeLimit(C)) { bEnd = true; bTimedOut = true; }
	}
	else
	{
		S.WallHangElapsed += C.Dt;
		if (S.WallHangElapsed > WallrunHangTimeLimit(C)) { bEnd = true; }
		if (!bAds) { bEnd = true; }
		(void)PrevHang; // hang start / complete sounds are FX only
	}

	const uint32_t Phase = S.TickCount % 4u; // [I] command-number phase (W4)
	if (Phase == 0)
	{
		if (WallProbe(C, S.Origin + TFVec3(0.f, 0.f, P.WallrunProbeLowOffset), N, CV.wallrun_allowed_wall_dist, nullptr) == 2 && !bEnd)
		{
			return;
		}
	}
	else
	{
		if (Phase == 2 && S.Velocity.Z > 0.f)
		{
			const TFVec3 Top = EyePos(C) + TFVec3(0.f, 0.f, P.WallrunTopProbeOffset);
			if (WallProbe(C, Top, N, CV.wallrun_allowed_wall_dist, nullptr) != 2)
			{
				S.Velocity.Z = std::max(0.f, S.Velocity.Z - C.Dt * P.WallrunTopDecelMultiplier * CV.wallrun_avoid_wall_top_decel);
			}
		}
		if (!bEnd) { return; }
	}
	EndWallRun(C, true, bTimedOut);
}

// -------------------------------------------------------------------------------------------
// Accelerate / friction (jump_air_friction.md sec.2, ground_misc.md sec.2.3)
// -------------------------------------------------------------------------------------------
void Accelerate(TFMoveCtx& C, const TFVec3& WishDir, float WishSpeed, float Accel, float Dt)
{
	TFPilotState& S = C.S;
	if (!C.In.bAlive || S.bHovering) { return; }
	const float Cap2 = std::max(S.Velocity.LengthSq(), WishSpeed * WishSpeed);
	const float Add = WishSpeed - S.Velocity.Dot(WishDir);
	if (Add <= 0.f) { return; }
	S.Velocity += WishDir * std::min(Add, Accel * Dt * S.SurfaceFriction); // absolute rate, no x wishspeed
	const float L2 = S.Velocity.LengthSq();
	if (L2 > Cap2 && L2 > 0.f) { S.Velocity *= std::sqrt(Cap2 / L2); }   // never above max(old, wish)
}

void AirAccelerate(TFMoveCtx& C, const TFVec3& WishDir, float WishSpeed, float Accel, float Dt)
{
	TFPilotState& S = C.S;
	if (!C.In.bAlive) { return; }
	const float Add = WishSpeed - S.Velocity.Dot(WishDir);
	if (Add > 0.f)
	{
		S.Velocity += WishDir * std::min(Add, Accel * Dt); // no 30-unit cap, no surface friction
	}
	else
	{
		// Steer only: player_extraairaccelleration, never gains speed.
		const float Cap2 = std::max(S.Velocity.LengthSq(), WishSpeed * WishSpeed);
		S.Velocity += WishDir * (Dt * C.CV.player_extraairaccelleration);
		const float L2 = S.Velocity.LengthSq();
		if (L2 > Cap2 && L2 > 0.f) { S.Velocity *= std::sqrt(Cap2 / L2); }
	}
}

float GroundAccel(const TFMoveCtx& C)
{
	const TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	const float H2 = S.Velocity.Length2DSq();
	float A;
	if (S.bSliding)
	{
		A = P.SlideAccel;
	}
	else if (P.SprintAcceleration >= 0.f && H2 > Sq(StanceSpeed(C, CurrentStance(C)) - 1.f))
	{
		A = P.SprintAcceleration;
	}
	else if (P.LowAcceleration >= 0.f && H2 < Sq(SpeedScale(C) * P.LowSpeed))
	{
		A = P.LowAcceleration;
	}
	else
	{
		A = P.Acceleration;
	}
	return A * P.AccelScale;
}

float GroundDecel(const TFMoveCtx& C, const TFVec3& WishDir)
{
	const TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	float D = -1.f;
	if (S.Velocity.Length() > StanceSpeed(C, CurrentStance(C)))
	{
		D = (P.SprintDeceleration >= 0.f) ? P.SprintDeceleration : (P.SprintAcceleration >= 0.f ? P.SprintAcceleration : -1.f);
	}
	if (S.bSliding)
	{
		if (S.CrouchMode == 3) { return 0.f; }
		D = (!IsDuckHeld(C) || WishDir.Dot(S.Velocity) < 0.f) ? P.SlideWantToStopDecel : P.SlideDecel;
	}
	if (D < 0.f) { D = (P.Deceleration >= 0.f) ? P.Deceleration : P.Acceleration * P.DecelFallbackFraction; }
	float Ramp = 1.f;
	if (C.Now < S.DamageImpulseNoDecelEndTime)
	{
		Ramp = Clamp01((C.Now - (S.DamageImpulseNoDecelEndTime - 0.5f)) * 2.f);
	}
	return Ramp * D * P.AccelScale;
}

// Pilot ground friction: the part of the velocity along the input (up to wishspeed) is protected;
// everything else loses a fixed amount per second. Sliding adds slideVelocityDecay^(k*dt).
void Friction(TFMoveCtx& C, const TFVec3& WishDir, float WishSpeed, float Dt)
{
	TFPilotState& S = C.S;
	const TFVec3 V = S.Velocity;
	const float D = WishDir.Dot(V);
	TFVec3 Keep;
	TFVec3 Rest;
	if (D <= 0.f || V.LengthSq() <= 0.f)
	{
		Rest = V;
	}
	else
	{
		const float F = std::min(1.f, D * WishSpeed / V.LengthSq());
		TFVec3 Dir = WishDir;
		const TFVec3& Lw = S.LastWallNormal;
		if (!Lw.IsNearlyZero() && Dir.Dot(Lw) < 0.f)
		{
			Dir += Lw * (1.f / 128.f - Lw.Dot(Dir));
			Dir = Dir.Normalized();
			Keep = Dir * (std::max(Dir.Dot(V), 0.f) * F);
		}
		else
		{
			Keep = Dir * (F * D);
		}
		Rest = V - Keep;
	}
	const float R = Rest.Length();
	if (R <= 0.f) { return; }
	float NewR = std::max(R - GroundDecel(C, WishDir) * S.SurfaceFriction * C.P.GroundFrictionScale * Dt, 0.f);
	if (S.bSliding)
	{
		// [I] S4: pow, and the exponent weakened by the speed_boost effect (G1).
		const float Expo = (1.f - C.P.SlideBoostDecayStimFactor * S.SpeedBoostSeverity) * Dt;
		NewR *= std::pow(C.P.SlideVelocityDecay, Expo);
	}
	if (NewR < R)
	{
		S.Velocity = Rest * (NewR / R) + Keep;
	}
}

// -------------------------------------------------------------------------------------------
// Gravity (FinishGravity / StartGravity half steps)
// -------------------------------------------------------------------------------------------
float GrappleGravityFrac(TFMoveCtx& C);

void GravityHalfStep(TFMoveCtx& C, float Dt)
{
	TFPilotState& S = C.S;
	if (S.bHovering && !C.CV.airslowmo_when_hovering) { return; } // hover cancels gravity
	// Entity gravity = gravityScale [I] (ASSUMPTIONS J1).
	const float G = C.P.GravityScale * C.CV.sv_gravity * Dt * 0.5f * WallRunGravityScale(C) * GrappleGravityFrac(C);
	S.Velocity.Z -= G;
	if (S.bSliding && S.bOnGround)
	{
		// Slope push while sliding: g_half * n.xy * n.z (n = ground normal [I], S3).
		const float GHalf = 0.5f * C.CV.sv_gravity * Dt * C.P.GravityScale;
		const TFVec3& N = S.GroundNormal;
		S.Velocity.X += GHalf * N.X * N.Z;
		S.Velocity.Y += GHalf * N.Y * N.Z;
	}
}

void FinishGravity(TFMoveCtx& C, float Dt) { GravityHalfStep(C, Dt); }

// -------------------------------------------------------------------------------------------
// Slide (slide_mantle.md sec.1)
// -------------------------------------------------------------------------------------------
void TryStartSlide(TFMoveCtx& C, const TFVec3& GroundNormal)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	if (S.bSliding) { return; }
	if (!(GroundNormal.Z >= P.WalkableNormalZ || C.CV.slide_whileInAir)) { return; }
	float S2;
	if (S.CrouchMode == 3)
	{
		S2 = std::max(S.Velocity.Length2DSq(), 0.01f);
	}
	else
	{
		if (!P.bSlideEnabled) { return; }
		S2 = S.Velocity.Length2DSq();
		if (S2 < Sq(P.SlideRequiredStartSpeed)) { return; }
		// [I] view forward taken as the flattened facing: the game passes mv+0x54 un-normalised;
		// using the 3D forward would forbid slides while looking down.
		if (S.bOnGround && C.YawFwd.Dot(C.WishDir) < C.CV.slide_max_angle_dot) { return; }
	}
	S.bSliding = true;
	if (S.DuckState == ETFDuckState::Standing) { BeginDucking(C); }

	if (C.Now - S.LastSlideTime > C.CV.slide_boost_cooldown)
	{
		const float Spd = std::sqrt(S2);
		const float Target = std::min(Spd + P.SlideSpeedBoost, P.SlideSpeedBoostCap);
		if (Target <= Spd)
		{
			S.LastSlideBoost = 0.f;
		}
		else
		{
			S.LastSlideBoost = Target - Spd;
			const float K = Target / Spd;
			S.Velocity.X *= K;
			S.Velocity.Y *= K;
		}
		S.bSlideLongJumpAllowed = true;
	}
	S.LastSlideTime = C.Now; // always, boosted or not
	Event(C, ETFMoveEvent::SlideStart, S.LastSlideBoost);
}

void EndSlide(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (!S.bSliding) { return; }
	S.bSliding = false;
	if (C.CV.slide_auto_stand && S.bDuckToggle) { S.bDuckToggle = false; }
	Event(C, ETFMoveEvent::SlideEnd);
}

// -------------------------------------------------------------------------------------------
// Step move (ground_misc.md sec.2.4)
// -------------------------------------------------------------------------------------------
void StepMove(TFMoveCtx& C, float Dt, TFMoveHits& Hits)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	const TFVec3 Start = S.Origin;
	const TFVec3 Vel0 = S.Velocity;
	const TFVec3 Up = TFVec3::Up();

	TFVec3 FlatPos = Start;
	TFVec3 FlatVel = Vel0;
	TFMoveHits FlatHits;
	const bool bFlatBlocked = SlideMove(C, FlatPos, FlatVel, Dt, &FlatHits);

	auto UseFlat = [&]()
	{
		S.Origin = FlatPos;
		S.Velocity = FlatVel;
		Hits = FlatHits;
	};

	if (!bFlatBlocked || P.StepHeight <= 0.f)
	{
		UseFlat();
		return;
	}

	// Raised move.
	const float StepSize = P.StepHeight;
	const TFTraceResult UpTr = Sweep(C, Start, Start + Up * (StepSize + kStepClearance));
	const float UpAmt = UpTr.bStartSolid ? 0.f : UpTr.Fraction * (StepSize + kStepClearance);
	TFVec3 StepPos = Start + Up * UpAmt;
	TFVec3 StepVel = Vel0;
	TFMoveHits StepHits;
	SlideMove(C, StepPos, StepVel, Dt, &StepHits);

	if ((StepPos - Start).Length2DSq() * kStepGain <= (FlatPos - Start).Length2DSq())
	{
		UseFlat();
		return;
	}

	const TFTraceResult Down = Sweep(C, StepPos, StepPos - Up * (UpAmt + StepSize + kStepClearance));
	if (Down.bStartSolid || !Down.Hit())
	{
		UseFlat();
		return;
	}
	const TFVec3 Landing = Down.EndPos;
	const float Rise = Landing.Z - Start.Z;
	if (Down.Normal.Z < P.WalkableNormalZ)
	{
		// Steep landing (the stepTestDist re-probe is simplified away [I]).
		const float Horiz2 = (Landing - Start).Length2DSq();
		const bool bAccept = Down.Normal.Z > 0.f && (Rise <= 0.f || Horiz2 / (Horiz2 + Rise * Rise) > kStepSlopeRatio);
		if (!bAccept)
		{
			UseFlat();
			return;
		}
	}
	if (std::fabs(Rise) > std::max(2.f * UpAmt, 0.1f * StepSize))
	{
		UseFlat();
		return;
	}

	S.Origin = Landing;
	S.Velocity = StepVel;
	Hits = StepHits;

	// Slide step-up penalty: stepHeight x slide_step_velocity_reduction u/s.
	if (S.bSliding && Rise > 0.f)
	{
		const float Spd = S.Velocity.Length2D();
		if (Spd > 0.f)
		{
			const float NewSpd = std::max(0.f, Spd - Rise * C.CV.slide_step_velocity_reduction);
			S.Velocity.X *= NewSpd / Spd;
			S.Velocity.Y *= NewSpd / Spd;
		}
	}
}

void StayOnGround(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (S.bWallRunning) { return; }
	TFVec3 Start = S.Origin + TFVec3(0.f, 0.f, 2.f);
	const TFTraceResult UpTr = Sweep(C, S.Origin, Start);
	Start = UpTr.bStartSolid ? S.Origin : UpTr.EndPos;
	const TFTraceResult Tr = Sweep(C, Start, S.Origin - TFVec3(0.f, 0.f, C.P.StepHeight));
	if (Tr.Fraction > 0.f && Tr.Fraction < 1.f && !Tr.bStartSolid && Tr.Normal.Z >= C.P.WalkableNormalZ)
	{
		S.Origin = Tr.EndPos;
	}
}

// -------------------------------------------------------------------------------------------
// CategorizePosition (ground_misc.md sec.2.5, wallrun.md)
// -------------------------------------------------------------------------------------------
void CategorizePosition(TFMoveCtx& C, bool bGrappleFlag)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	if (S.Zipline.State != 0 || S.Mantle.bActive) { return; }

	S.SurfaceFriction = 1.f;
	S.PushAwayFromTopAccel = 0.f;
	const bool bWall = S.bWallRunning;
	const float Probe = (S.bOnGround || bWall) ? P.StepHeight : kAirProbeDist;
	const float UpSpeed = (S.Velocity - S.BaseVelocity).Dot(S.UpDir);

	const bool bLeave = (!bWall && UpSpeed > P.LeaveGroundUpSpeed)
		|| (bGrappleFlag && (!bWall || UpSpeed > 1.f))
		|| S.bDodgingInAir;
	if (bLeave)
	{
		if (UpSpeed > 0.f) { S.SurfaceFriction = P.AirSurfaceFriction; }
		if (bWall) { StopWallRun(C); }
		SetGround(C, false, TFVec3::Up());
		return;
	}

	const TFTraceResult Tr = Sweep(C, S.Origin, S.Origin - S.UpDir * Probe);
	const bool bHit = Tr.Hit() && !Tr.Normal.IsNearlyZero();
	if (!bHit)
	{
		if (bWall)
		{
			// Lost the wall: the next FullWalkMove ends the wallrun because the ground is NULL.
			// [I] treated like a WallRunThink end without the fall-away push.
			EndWallRun(C, false, false);
		}
		SetGround(C, false, TFVec3::Up());
		return;
	}

	const TFVec3 N = Tr.Normal;
	if (bWall)
	{
		if (N.Z >= P.WalkableNormalZ)
		{
			if (C.Now - S.WallRunStartTime > P.WallrunCategorizeLandTime)
			{
				StopWallRun(C);
				SetGround(C, true, N);
				S.OnSlopeTime = C.Now;
				S.TimeLastTouchedGround = C.Now;
				S.UpDirWhenLastTouchedGround = S.UpDir;
			}
			return;
		}
		if (N.Z > P.WallrunMinNormalZ && N.Z < P.WallrunMaxNormalZ)
		{
			const float FwdDot = N.Dot(S.WallNormal);
			if (FwdDot < 0.999f) { RotateWallNormal(C, N); }
			// wallrun_angleChangeMinCos only gates snapping onto the new surface.
			if (FwdDot >= C.CV.wallrun_angleChangeMinCos && Tr.Fraction > 0.f && Tr.Fraction < 1.f && !Tr.bStartSolid)
			{
				S.Origin = Tr.EndPos;
			}
		}
		S.TimeLastTouchedGround = C.Now;
		S.UpDirWhenLastTouchedGround = S.UpDir;
		return;
	}

	if (N.Z < P.WalkableNormalZ)
	{
		SetGround(C, false, TFVec3::Up());
		if (UpSpeed > 0.f) { S.SurfaceFriction = P.AirSurfaceFriction; }
		if (N.Z >= P.WallrunMinNormalZ && C.Now - S.WallRunStartTime > P.WallrunCategorizeRestartTime)
		{
			TryStartWallRun(C, Tr.EndPos, N, S.Velocity);
		}
		return;
	}

	const bool bWasGround = S.bOnGround;
	SetGround(C, true, N);
	if (bWasGround && Tr.Fraction > 0.f && Tr.Fraction < 1.f && !Tr.bStartSolid)
	{
		S.Origin = Tr.EndPos;
	}
	S.OnSlopeTime = C.Now; // [I] W1 reading: last time on the ground
	S.TimeLastTouchedGround = C.Now;
	S.UpDirWhenLastTouchedGround = S.UpDir;
}

// -------------------------------------------------------------------------------------------
// Dodge (ground_misc.md sec.3.1). Present but disabled for MP pilots (dodge 0 [D]).
// -------------------------------------------------------------------------------------------
void UpdateDodgeState(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (S.bDodgingInAir && C.Now - S.LastDodgeTime >= C.P.DodgeDuration) { S.bDodgingInAir = false; }
	if (S.Velocity.Length() < C.P.DodgeStopSpeed) { S.bDodgingInAir = false; S.bDodging = false; }
	if (S.bDodging && C.Now - S.LastDodgeTime > C.P.DodgeAnimDuration) { S.bDodging = false; }
}

// -------------------------------------------------------------------------------------------
// Wall jump (wallrun.md fn_180173a00)
// -------------------------------------------------------------------------------------------
void WallJump(TFMoveCtx& C, const TFVec3& N)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	TFVec3 InDir = C.WishDir;
	if (S.bWallHanging)
	{
		S.bAdsLatch = false;
		const float L = InDir.Length();
		if (L < 1.f) { InDir += C.YawFwd * (1.f - L); } // pad weak input toward facing
	}
	TFVec3 J = InDir * P.WallrunJumpInputDirSpeed;
	const float A = std::min(0.f, N.Dot(C.ViewFwd));
	const float B = std::min(0.f, N.Dot(InDir));
	const float Out = (1.f - A * B * P.WallrunJumpIntoWallCut) * P.WallrunJumpOutwardSpeed;
	if (N.Dot(J) < Out) { J += N * (Out - N.Dot(J)); }
	RecordRepelWall(C, N);
	S.Velocity += J;
	S.bWallRunWeak = false;
	const float UpSpd = WallRunStrength(C) * P.WallrunJumpUpSpeed; // strength is 1 now (weak cleared)
	if (S.Velocity.Z < UpSpd)
	{
		S.Velocity.Z = std::min(S.Velocity.Z + P.WallrunJumpUpMultiplier * UpSpd, UpSpd);
	}
}

// -------------------------------------------------------------------------------------------
// CheckJumpButton (jump_air_friction.md sec.2): ground, coyote, skip, slide-jump, double, wall, dodge
// -------------------------------------------------------------------------------------------
bool CanDoubleJump(const TFMoveCtx& C)
{
	return C.S.SuperjumpPower >= C.CV.superjump_min_power_use && C.P.bDoubleJump
		&& static_cast<float>(C.S.AirJumpCount) < C.P.SuperjumpLimit;
}

bool CheckJumpButton(TFMoveCtx& C, bool bForcedGroundJump)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	const TFMovementConVars& CV = C.CV;
	if (!C.In.bAlive) { return false; }
	if (!bForcedGroundJump && !C.bJumpPressed) { return false; } // no separate dodge button for pilots

	bool bIsDodge = false;
	const bool bHasSurface = S.bOnGround || S.bWallRunning;
	bool bGroundJump = false;
	TFVec3 JumpNormal = S.UpDir;
	if (bForcedGroundJump)
	{
		bGroundJump = true;
		JumpNormal = TFVec3::Up();
	}
	else if (!bHasSurface)
	{
		// Coyote time: counts as a ground jump with the up-direction from the last contact.
		if (C.Now - S.TimeLastTouchedGround < CV.jump_graceperiod && !S.bHasJumpedSinceTouchedGround)
		{
			bGroundJump = true;
			JumpNormal = S.UpDirWhenLastTouchedGround;
		}
	}
	else
	{
		bGroundJump = true;
	}
	// Second, class-specific window (jetpack class only; boostEnabled 0 for MP pilots).
	if (P.bBoostEnabled && C.Now - S.TimeLastTouchedGround <= P.BoostOnGroundSafety && !S.bHasJumpedSinceTouchedGround)
	{
		bGroundJump = true;
	}

	bool bIsDouble = false;
	if (!bGroundJump && !bIsDodge)
	{
		if (P.bDoubleJump) { bIsDouble = true; }
		else { bIsDodge = true; }
	}
	if (S.DuckState == ETFDuckState::Unducking && !S.bSliding) { return false; }

	bool bVertDodge = false;
	if (bIsDouble)
	{
		if (P.SuperjumpLimit <= 0.f) { return false; }
		if (!CanDoubleJump(C)) { return false; }
	}
	else if (bIsDodge)
	{
		if (S.DuckState != ETFDuckState::Standing || !P.bDodgeEnabled) { return false; }
		if (S.LastDodgeTime + P.DodgeInterval > C.Now || S.SuitPower < P.DodgePowerDrain) { return false; }
		if (!CV.dodge_no_movement_forward_enable && C.WishDir.LengthSq() <= 1e-7f) { return false; }
		S.SuitPower = std::max(0.f, S.SuitPower - P.DodgePowerDrain);
		S.TimeAllSuitDevicesOff = std::max(S.TimeAllSuitDevicesOff, C.Now + P.DodgePowerDelay);
		bVertDodge = CV.dodge_vertical_enable && C.WishDir.Length() < CV.dodge_vertical_threshold;
	}
	else
	{
		if (!P.bJumpEnabled) { return false; }
		S.OnSlopeTime = 0.f;
	}

	const bool bWasWallRunning = S.bWallRunning;
	const bool bWasSliding = S.bSliding;
	SetGround(C, false, TFVec3::Up());
	const float GroundFactor = 1.f; // surface jumpFactor (no surface data here)

	// Slide long-jump: strip the boost within 0.4 s of the slide start, snap 85..100% of max to max.
	if (S.bSliding && S.bSlideLongJumpAllowed)
	{
		const float H2 = S.Velocity.Length2DSq();
		if (H2 > 0.f)
		{
			const float Hs = std::sqrt(H2);
			float Ns = Hs;
			// [I] S2: duck state 2 read as "fully ducked", so the strip window may be < 0.4 s.
			if (S.DuckState != ETFDuckState::Ducked && C.Now - P.SlideBoostStripWindow < S.LastSlideTime)
			{
				Ns = std::max(Hs - S.LastSlideBoost, 0.f);
				S.Velocity *= Ns / Hs;
			}
			const float Cap = P.SlideMaxJumpSpeed;
			if (Sq(P.SlideJumpSnapBand * Cap) < H2 && H2 < Cap * Cap && Ns > 0.f)
			{
				S.Velocity *= Cap / Ns;
			}
			S.bSlideLongJumpAllowed = false;
		}
	}

	const bool bIsWallJump = JumpNormal.Z < 0.9999f && !bIsDodge;
	float H = 0.f;
	bool bIsSkip = false;
	if (bIsDouble)
	{
		const float T = 0.f; // the interpolation input is hard-coded 0 [R]; superjumpMaxHeight unused
		const float FullH = LerpF(P.SuperjumpMinHeight, P.SuperjumpMaxHeight, T);
		const float Rise = S.Velocity.Z >= 0.f ? Sq(S.Velocity.Z) / (2.f * CV.sv_gravity) : 0.f;
		H = FullH * CV.superjump_min_height_fraction;
		if (H + Rise < FullH)
		{
			S.Velocity.Z = 0.f;
			H = FullH;
		}
		S.SuperjumpPower = Clamp(S.SuperjumpPower - LerpF(CV.superjump_min_power_use, CV.superjump_max_power_use, T), 0.f, 100.f);
		S.AirJumpCount++;
	}
	else if (bIsDodge)
	{
		H = bVertDodge ? P.DodgeVerticalHeight : P.DodgeHeight;
	}
	else
	{
		H = bWasSliding ? P.SlideJumpHeight : P.JumpHeight;
		if (C.Now - S.TimeLastLanded < CV.skip_time && !bWasWallRunning)
		{
			bIsSkip = true;
			H *= CV.skip_jump_height_fraction;
			const float Hs = S.Velocity.Length2D();
			if (Hs > CV.skip_speed_retain)
			{
				const float K = std::max(Hs - CV.skip_speed_reduce, CV.skip_speed_retain) / Hs;
				S.Velocity.X *= K;
				S.Velocity.Y *= K;
			}
		}
	}

	// The impulse uses raw sv_gravity (not x gravityScale) [R].
	float V = std::sqrt(2.f * CV.sv_gravity * H);
	S.bHasJumpedSinceTouchedGround = true;

	if (bIsWallJump)
	{
		WallJump(C, JumpNormal);
		if (bWasWallRunning) { StopWallRun(C); } // ground is NULL now, FullWalkMove ends the run
		Event(C, ETFMoveEvent::WallJump);
	}
	else
	{
		if (S.Grapple.bActive && S.Grapple.bPulling) { V *= CV.grapple_jumpFrac; }
		S.WallRunCount = 0;
		S.bWallRunWeak = false;
		S.bHovering = false;
		if (CV.dodge_vertical_in_air || !bIsDodge || bGroundJump)
		{
			S.Velocity += S.UpDir * (GroundFactor * V); // additive along m_upDir
		}
		if (!S.LastWallNormal.IsNearlyZero() && C.WishDir.Dot(S.LastWallNormal) < 0.f)
		{
			S.Velocity -= S.LastWallNormal * P.WallNudgeSpeed;
		}
		if (bIsDouble || bIsDodge)
		{
			TFVec3 Wv;
			if (!bVertDodge)
			{
				if (bIsDodge)
				{
					Wv = C.WishDir.Normalized2D();
					if (Wv.IsNearlyZero() && CV.dodge_no_movement_forward_enable) { Wv = C.YawFwd; }
					Wv *= P.DodgeSpeed * (1.f - S.MoveSlowSeverity);
				}
				else
				{
					Wv = C.WishDir.Flat() * P.SuperjumpHorzSpeed;
				}
			}
			const float Hv = S.Velocity.Length2D();
			const float Wl = Wv.Length2D();
			float Keep = (Wl < Hv && Hv > 0.f) ? 1.f - Wl / Hv : 0.f;
			if (bVertDodge) { Keep *= CV.dodge_vertical_horzspeedscale; }
			if (bIsDodge) { Keep += (1.f - Keep) * P.DodgeKeepSpeedFrac; }
			S.Velocity.X = Keep * S.Velocity.X + Wv.X;
			S.Velocity.Y = Keep * S.Velocity.Y + Wv.Y;
			if (bIsDodge)
			{
				S.LastDodgeTime = C.Now;
				S.bDodgingInAir = true;
				S.bDodging = true;
				Event(C, ETFMoveEvent::Dodge);
			}
		}
		if (bIsDouble) { Event(C, ETFMoveEvent::DoubleJump); }
		else if (!bIsDodge)
		{
			Event(C, bWasSliding ? ETFMoveEvent::SlideJump : (bIsSkip ? ETFMoveEvent::SkipJump : ETFMoveEvent::Jump));
		}
	}

	EndSlide(C);
	FinishGravity(C, C.MoveDt);
	CheckVelocity(C);
	S.TimeLastJumped = C.Now;
	return true;
}

// -------------------------------------------------------------------------------------------
// Grapple (grapple_zipline.md sec.1). World hooks only: no reel-in targets, auto-melee, rodeo,
// titan embark or window hints (those need other entities).
// -------------------------------------------------------------------------------------------
TFVec3 GrapplePullVector(const TFMoveCtx& C)
{
	return C.S.Grapple.Points[0] + TFVec3(0.f, 0.f, C.CV.grapple_lift) - EyePos(C);
}

bool GrapplePullingNow(const TFMoveCtx& C)
{
	const TFGrappleState& G = C.S.Grapple;
	return G.bActive && G.bAttached && G.bPulling && G.PointCount > 0;
}

float GrappleRange(const TFMoveCtx& C, const TFVec3& Dir)
{
	// Ellipse: L / sqrt(((L/Lv)^2 - 1) * dir.z^2 + 1).
	const float L = C.P.GrappleMaxLength;
	const float Lv = std::max(C.P.GrappleMaxLengthVert, 1.f);
	return L / std::sqrt((Sq(L / Lv) - 1.f) * Sq(Dir.Z) + 1.f);
}

float GrappleGravityFrac(TFMoveCtx& C)
{
	if (!GrapplePullingNow(C)) { return 1.f; }
	const TFVec3 D = GrapplePullVector(C);
	const float D2 = D.LengthSq();
	if (D2 <= 0.f) { return 1.f; }
	if (D.Z < 0.f && Sq(D.Z) / D2 > Sq(C.CV.grapple_letGravityHelpCosAngle)) { return 1.f; } // point steeply below
	const TFVec3 Dn = D / std::sqrt(D2);
	// pushUnder: forward input perpendicular to the rope, pointing under it, only while looking down.
	// The exact vector construction is [I]; the look-down gate clamp01(-10*fm*fwd.z) is [R].
	const float LookDown = Clamp01(-10.f * C.ForwardMove * C.ViewFwd.Z);
	const TFVec3 W = C.ViewFwd * C.ForwardMove;
	const TFVec3 WPerp = W - Dn * W.Dot(Dn);
	const float PushUnder = std::max(0.f, -WPerp.Z) * LookDown;
	const float AwayXY = std::max(0.f, -C.WishDir.Dot2D(D.Normalized2D()));
	const float T = std::max(std::min(1.f, PushUnder * C.CV.grapple_gravityPushUnderContribution), AwayXY);
	return C.P.GrappleGravityFracMin + (C.P.GrappleGravityFracMax - C.P.GrappleGravityFracMin) * T;
}

void GrappleInitialImpulse(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const TFMovementConVars& CV = C.CV;
	const TFVec3 Dir = GrapplePullVector(C).Normalized();
	if (Dir.IsNearlyZero()) { return; }
	TFVec3& V = S.Velocity;
	const float A = std::max(0.f, V.Dot(Dir));
	V.X = (V.X - Dir.X * A) * CV.grapple_initialSlowFrac_human + Dir.X * A;
	V.Y = (V.Y - Dir.Y * A) * CV.grapple_initialSlowFrac_human + Dir.Y * A;
	V.Z = (V.Z - Dir.Z * A) * CV.grapple_initialSlowFracVert_human + Dir.Z * A;
	const float A2 = V.Dot(Dir);
	const float SlowK = 1.f; // grapple_slow fraction (FUN_180222030) not available [I]: 0
	const float Cap = SlowK * CV.grapple_speedRampMin_human;
	if (A2 < Cap)
	{
		// As decompiled: max first, then min -> the 350 impulse is capped at speedRampMin (50) [R], GR1.
		float T = std::max(A2 + SlowK * CV.grapple_initialImpulse_human, CV.grapple_initialSpeedMin_human);
		T = std::min(T, Cap);
		V += Dir * (T - A2);
	}
	// Off-ground impulse along m_upDir.
	if (S.bOnGround || S.bWallRunning)
	{
		const float Add = std::max(0.f, -S.UpDir.Dot(V)) + CV.grapple_initialImpulseOffGround_human;
		V += S.UpDir * Add;
	}
	if (S.bWallRunning) { StopWallRun(C); }
	SetGround(C, false, TFVec3::Up());
}

void GrappleStartPull(TFMoveCtx& C)
{
	TFGrappleState& G = C.S.Grapple;
	G.bPulling = true;
	G.PullTime = C.Now;
	if (G.HitEntityId != 0)
	{
		// Reeling an entity: the whole power cost is charged at once.
		G.UsedPower = C.P.GrapplePowerRequired;
		C.S.GrapplePower = std::max(0.f, C.S.GrapplePower - C.P.GrapplePowerRequired);
		C.S.LastGrapplePowerUse = C.Now;
	}
	GrappleInitialImpulse(C);
	if (C.S.Zipline.State != 0) { ZiplineDetach(C); }
	Event(C, ETFMoveEvent::GrapplePullStart);
}

void GrappleAttach(TFMoveCtx& C, const TFVec3& Pos, int64_t EntityId)
{
	TFGrappleState& G = C.S.Grapple;
	if (!G.bAttached) { G.AttachTime = C.Now; }
	const bool bWasRetracting = G.bRetracting;
	G.bAttached = true;
	G.bRetracting = false;
	G.Phase = ETFHookPhase::Attached;
	if (G.PointCount < 1) { G.PointCount = 1; }
	G.Points[G.PointCount - 1] = Pos;
	G.HitEntityId = EntityId;
	Event(C, ETFMoveEvent::GrappleAttach);
	if (!G.bPulling && bWasRetracting) { GrappleStartPull(C); } // a hit on the way back skips the delay
}

void GrappleKillHook(TFMoveCtx& C)
{
	TFGrappleState& G = C.S.Grapple;
	G.bActive = false;
	G.bAttached = false;
	G.bPulling = false;
	G.bRetracting = false;
	G.bForcedRetracting = true;
	G.Phase = (G.PointCount > 0) ? ETFHookPhase::Retracting : ETFHookPhase::None; // visual return only
}

// Detach speed loss (grapple_zipline.md sec.1.6a): s = max(s - L, M) when s > M, 3D, direction kept.
void GrappleDetachSpeedLoss(TFMoveCtx& C, TFVec3& Vel)
{
	const float L = C.P.GrappleDetachSpeedLoss;
	const float M = C.P.GrappleDetachSpeedLossMin;
	const float Spd = Vel.Length();
	if (Spd * Spd > M * M)
	{
		const float NewSpd = std::max(Spd - L, M);
		Vel *= NewSpd / Spd;
	}
}

void GrappleDetach(TFMoveCtx& C, bool bApplyEffects)
{
	TFGrappleState& G = C.S.Grapple;
	if (!G.bActive || G.PointCount == 0) { return; }
	// Only when the pull had started; applied after any detach/impact vertical boost.
	if (bApplyEffects && G.bPulling) { GrappleDetachSpeedLoss(C, C.S.Velocity); }
	G.DetachTime = C.Now;
	GrappleKillHook(C);
	Event(C, ETFMoveEvent::GrappleDetach);
}

void GrappleVerticalBoost(TFVec3& Vel, float Boost, float Cap)
{
	if (Vel.Z < Cap) { Vel.Z = std::min(Vel.Z + Boost, Cap); }
}

// Grapple_ShouldDetach for world hooks (sec.1.6). May write the vertical boosts into the velocity.
bool GrappleShouldDetach(TFMoveCtx& C, bool& bOutSuppressEffects)
{
	TFPilotState& S = C.S;
	TFGrappleState& G = S.Grapple;
	const TFPilotSettings& P = C.P;
	bOutSuppressEffects = false;

	const TFVec3 Eye = EyePos(C);
	const TFVec3 Tip = G.Points[G.PointCount - 1];
	const float Len = std::min((Tip - S.Origin).Length(), (Tip - Eye).Length());
	const TFVec3 D = GrapplePullVector(C);
	const TFVec3 Dn = D.Normalized();
	const float VAlong = S.Velocity.Dot(Dn);

	// 2. Line too long.
	if (Len > std::max(P.GrappleMaxLength, P.GrappleMaxLengthVert) + P.GrappleLengthMargin) { return true; }

	// 3. Too short; the threshold grows with speed toward the point. Impact boost on a world hook.
	if (Len < P.GrappleDetachLengthMax
		&& Len < P.GrappleDetachLengthMin + (VAlong / C.CV.grapple_speedRampMax_human) * P.GrappleMinLengthSpeedScale * (P.GrappleDetachLengthMax - P.GrappleDetachLengthMin))
	{
		if (G.bPulling && G.HitEntityId == 0)
		{
			GrappleVerticalBoost(S.Velocity, P.GrappleImpactVerticalBoost, P.GrappleImpactVerticalMaxSpeed);
		}
		return true;
	}

	// 4. Target dead / invalid.
	if (!C.W.IsGrappleTargetValid(G.HitEntityId)) { return true; }

	// 5. Titan embark: no titans in this example.

	// 6. Passed the point / moving away.
	if (G.bPulling)
	{
		const bool bMovingAway = S.Velocity.Dot(D) < 0.f && G.HitEntityId == 0;
		bool bPassed = D.Dot(C.ViewFwd) < 0.f;
		if (bPassed && C.ViewFwd.Z < 0.f)
		{
			// Looking down: pull vector and tip must also be behind the view in XY.
			bPassed = D.Dot2D(C.ViewFwd) < 0.f && (Tip - Eye).Dot2D(C.ViewFwd) < 0.f;
		}
		const float SlowMo = LerpF(1.f, C.P.AirSlowMoSpeed, S.AirSlowMoBlend);
		if ((bMovingAway && VAlong * VAlong > Sq(P.GrappleDetachAwaySpeed)) || (bPassed && bMovingAway))
		{
			GrappleVerticalBoost(S.Velocity, P.GrappleDetachVerticalBoost, P.GrappleDetachVerticalMaxSpeed);
			return true;
		}
		if (bPassed && !bMovingAway && SlowMo >= 1.f) { return true; }
	}

	// 7. Low speed for too long.
	if (G.bPulling && !G.bHasGoodVelocity)
	{
		float Limit = P.GrappleDetachLowSpeedTime;
		if (S.bWallRunning) { Limit = P.GrappleDetachLowSpeedWallTime; }
		else if (S.bOnGround) { Limit = P.GrappleDetachLowSpeedGroundTime; }
		if (C.Now - G.LastGoodVelocityTime > Limit) { return true; }
	}

	// 8. Window entry: window hints are not implemented.
	return false;
}

void GrappleServerThink(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	TFGrappleState& G = S.Grapple;
	const TFPilotSettings& P = C.P;

	const TFVec3 D = GrapplePullVector(C);
	bool bGood = true;
	if (G.bPulling)
	{
		if (C.Now > G.PullTime + C.CV.grapple_gracePeriod)
		{
			const float Amt = std::max(0.f, std::min(P.GrapplePowerUseRate * C.Dt, P.GrapplePowerRequired - G.UsedPower - 0.001f));
			G.UsedPower += Amt;
			S.GrapplePower = std::max(0.f, S.GrapplePower - Amt);
			if (Amt > 0.f) { S.LastGrapplePowerUse = C.Now; }
		}
		const float Vd = S.Velocity.Dot(D);
		const float D2 = std::max(D.LengthSq(), 1e-6f);
		bGood = Vd > 0.f && (Vd * Vd) / D2 > Sq(P.GrappleDetachLowSpeedThreshold);
	}
	if (G.bHasGoodVelocity && !bGood) { G.LastGoodVelocityTime = C.Now; }
	G.bHasGoodVelocity = bGood;

	bool bSuppress = false;
	if (GrappleShouldDetach(C, bSuppress))
	{
		GrappleDetach(C, !bSuppress);
	}
	else if (!G.bPulling && C.Now - G.AttachTime > C.CV.grapple_pullDelay_human)
	{
		GrappleStartPull(C);
	}
}

void GrappleRetractStep(TFMoveCtx& C)
{
	TFGrappleState& G = C.S.Grapple;
	const TFVec3 Eye = EyePos(C);
	const float Speed = (G.bForcedRetracting || G.PointCount > 1) ? C.CV.grapple_forcedRetractVel : C.CV.grapple_retractVel;
	float Step = Speed * C.Dt;
	for (int Guard = 0; Guard < kMaxGrapplePoints + 1; ++Guard)
	{
		if (G.PointCount <= 0) { G.Phase = ETFHookPhase::None; G.bActive = false; return; }
		const TFVec3 Tip = G.Points[G.PointCount - 1];
		if (G.PointCount < 2)
		{
			const TFVec3 ToEye = Eye - Tip;
			if (ToEye.Length() <= Step)
			{
				G.PointCount = 0;
				G.Phase = ETFHookPhase::None;
				G.bActive = false;
				G.bRetracting = false;
				G.bForcedRetracting = false;
				return;
			}
			TFVec3 NewTip = Tip + ToEye.Normalized() * Step;
			if (!G.bForcedRetracting && G.bActive)
			{
				NewTip.Z -= C.Dt * C.CV.grapple_fallSpeed;
				const TFTraceResult Tr = C.W.TraceLine(Tip, NewTip);
				if (Tr.Hit() && !Tr.bStartSolid)
				{
					if (!Tr.bNoGrapple) { GrappleAttach(C, Tr.EndPos, Tr.HitEntityId); }
					else { G.bForcedRetracting = true; }
					return;
				}
			}
			G.Points[0] = NewTip;
			return;
		}
		const TFVec3 Prev = G.Points[G.PointCount - 2];
		const float SegLen = (Prev - Tip).Length();
		if (SegLen > Step)
		{
			G.Points[G.PointCount - 1] = Tip + (Prev - Tip) * (Step / SegLen);
			return;
		}
		G.PointCount--;
		Step -= SegLen;
	}
}

void GrappleFire(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	TFGrappleState& G = S.Grapple;
	if (G.Phase != ETFHookPhase::None) { return; }
	// [I] firing gate: enough grapple power for one full grapple (the gate is not in the docs).
	if (S.GrapplePower < C.P.GrapplePowerRequired) { return; }
	G = TFGrappleState();
	G.bActive = true;
	G.Phase = ETFHookPhase::Flying;
	G.PointCount = 1;
	G.Points[0] = EyePos(C);
	G.HookVel = C.ViewFwd * C.CV.grapple_shootVel; // edge aim-assist toward ledge hints omitted
	G.bHasGoodVelocity = true;
	Event(C, ETFMoveEvent::GrappleFire);
}

// Per-tick hook update: input, flight, retraction and the detach / pull-start think.
void GrappleUpdate(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	TFGrappleState& G = S.Grapple;
	if (!C.In.bAlive)
	{
		if (G.bActive) { GrappleDetach(C, false); }
		return;
	}

	if (C.In.bGrapplePressed)
	{
		if (G.Phase == ETFHookPhase::None) { GrappleFire(C); }
		else if (G.bActive && (G.Phase == ETFHookPhase::Attached || G.Phase == ETFHookPhase::Flying))
		{
			// [I] pressing the ability again releases the line; treated like the manual release.
			if (G.Phase == ETFHookPhase::Attached) { GrappleDetach(C, true); }
			else { G.bRetracting = true; G.bForcedRetracting = true; G.Phase = ETFHookPhase::Retracting; G.bActive = false; }
		}
	}
	// Manual crouch release (buttons 0x4000004 [I]) costs speed like any detach.
	if (G.bActive && G.bAttached && C.In.bCrouchHeld) { GrappleDetach(C, true); }

	switch (G.Phase)
	{
	case ETFHookPhase::Flying:
	{
		// [I] outward flight: the tip moves at grapple_shootVel and attaches on the first hit.
		const TFVec3 Tip = G.Points[0];
		const TFVec3 NewTip = Tip + G.HookVel * C.Dt;
		const TFTraceResult Tr = C.W.TraceLine(Tip, NewTip);
		if (Tr.Hit() && !Tr.bStartSolid)
		{
			if (!Tr.bNoGrapple) { GrappleAttach(C, Tr.EndPos, Tr.HitEntityId); }
			else { G.Points[0] = Tr.EndPos; G.bForcedRetracting = true; G.Phase = ETFHookPhase::Retracting; G.bActive = false; }
			break;
		}
		G.Points[0] = NewTip;
		const TFVec3 FromEye = NewTip - EyePos(C);
		if (FromEye.Length() > GrappleRange(C, FromEye.Normalized()))
		{
			G.bRetracting = true; // missed: return at retractVel, may still attach on the way
			G.Phase = ETFHookPhase::Retracting;
		}
		break;
	}
	case ETFHookPhase::Retracting:
		GrappleRetractStep(C);
		break;
	case ETFHookPhase::Attached:
		GrappleServerThink(C);
		break;
	case ETFHookPhase::None:
	default:
		break;
	}
}

// Per-tick pull (GrappleAccel + leave-ground + around-obstacle push). Returns the grapple flag.
bool GrapplePull(TFMoveCtx& C, float Dt)
{
	TFPilotState& S = C.S;
	TFGrappleState& G = S.Grapple;
	const TFMovementConVars& CV = C.CV;
	if (!GrapplePullingNow(C)) { return false; }

	const TFVec3 D = GrapplePullVector(C);
	const float Dist = D.Length();
	if (Dist < 1e-3f) { return true; }
	const TFVec3 Dir = D / Dist;

	TFVec3 V = S.Velocity;
	float A = V.Dot(Dir);
	TFVec3 Perp = V - Dir * A;
	TFVec3 Keep;
	if (CV.grapple_dontFightGravity && Perp.Z < 0.f)
	{
		Keep.Z = Perp.Z;
		Perp.Z = 0.f;
	}
	// Exempt part along the pulled entity's normal (vfunc 0x998), read as m_upDir [I].
	const TFVec3 Nup = S.UpDir;
	const float K = Nup.Dot(Perp);
	if (K > 0.f)
	{
		Perp -= Nup * K;
		Keep += Nup * K;
	}
	const float PerpLen = Perp.Length();
	const float NewPerp = std::max(0.f, PerpLen - CV.grapple_decel_human * Dt);
	const TFVec3 PerpDir = PerpLen > 1e-6f ? Perp / PerpLen : TFVec3();

	const float RampT = Clamp01((C.Now - G.PullTime) / std::max(CV.grapple_speedRampTime_human, 1e-4f));
	const float Cap = LerpF(CV.grapple_speedRampMin_human, CV.grapple_speedRampMax_human, RampT); // x (1 - slow), slow = 0
	if (A < Cap) { A = std::min(A + CV.grapple_accel_human * Dt, Cap); } // speed above the cap is kept

	const float OldVz = V.Z;
	TFVec3 NewV = Dir * A + PerpDir * NewPerp + Keep;
	if (CV.grapple_dontFightGravity) { NewV.Z = std::max(OldVz, NewV.Z); } // never lowers vz
	S.Velocity = NewV;

	// Leave the ground / wall.
	if (S.bOnGround && TFVec3::Up().Dot(Dir) >= kGrappleLeaveGroundDot)
	{
		SetGround(C, false, TFVec3::Up());
	}
	if (S.bWallRunning && S.WallNormal.Dot(Dir) >= kGrappleLeaveWallDot)
	{
		StopWallRun(C); // [I] the wall stops being the ground entity
		SetGround(C, false, TFVec3::Up());
	}

	// Around-obstacle push when pulling into a surface.
	TFVec3 N = S.LastWallNormal;
	if (std::max(std::fabs(N.X), std::max(std::fabs(N.Y), std::fabs(N.Z))) <= 0.01f && S.bOnGround) { N = S.UpDir; }
	if (N.Dot(Dir) < kGrappleObstacleTrigger)
	{
		const float HullH = HullHeight(C);
		const float R = C.P.HullRadius;
		TFVec3 ProbeDir;
		float Step = 0.f;
		switch (S.TickCount % 4u)
		{
		case 0: ProbeDir = TFVec3::Up(); Step = HullH * 0.4f; break;
		case 1: ProbeDir = TFVec3(N.Y, -N.X, 0.f).Normalized(); Step = R * 0.4f; break;
		case 2: ProbeDir = TFVec3(-N.Y, N.X, 0.f).Normalized(); Step = R * 0.4f; break;
		default: ProbeDir = TFVec3(0.f, 0.f, -1.f); Step = HullH * 0.15f; break;
		}
		if (!ProbeDir.IsNearlyZero())
		{
			for (int I = 1; I <= 2; ++I)
			{
				const TFVec3 Pt = S.Origin + ProbeDir * (Step * static_cast<float>(I));
				if (!Sweep(C, Pt, Pt + Dir * R).Hit() && !Sweep(C, S.Origin, Pt).Hit())
				{
					S.Velocity += ProbeDir * (CV.grapple_around_obstacle_accel * kGrappleObstacleAccelMul * Dt);
					break;
				}
			}
		}
	}
	return true;
}

// Line wrap around corners (sec.1.5 item 13), simplified [I]: corner from the two hit planes, falling
// back to the midpoint; no unwrapping (the docs describe none).
void GrappleLineWrap(TFMoveCtx& C)
{
	TFGrappleState& G = C.S.Grapple;
	if (!GrapplePullingNow(C)) { return; }
	const TFVec3 Eye = EyePos(C);
	const TFVec3 P0 = G.Points[0];
	const TFVec3 ToP = P0 - Eye;
	const float L = ToP.Length();
	if (L < 2.f) { return; }
	const TFVec3 Dir = ToP / L;
	const TFTraceResult A = C.W.TraceLine(Eye, P0 - Dir * 1.f);
	if (!A.Hit() || A.Fraction > 0.999f || A.bStartSolid) { return; }
	const TFTraceResult B = C.W.TraceLine(P0 - Dir * 1.f, Eye);
	if (!B.Hit() || B.Fraction > 0.999f || B.bStartSolid) { return; }
	if (A.bNoGrapple || B.bNoGrapple) { GrappleDetach(C, true); return; }
	if (G.PointCount >= kMaxGrapplePoints) { return; } // >3 points: the game only unparents the hook

	const TFVec3 Mid = (A.EndPos + B.EndPos) * 0.5f;
	TFVec3 Corner = Mid;
	const TFVec3 Na = A.Normal;
	const TFVec3 Nb = B.Normal;
	const TFVec3 Line = Na.Cross(Nb);
	if (Line.Length() > 0.1f)
	{
		const float Da = Na.Dot(A.EndPos);
		const float Db = Nb.Dot(B.EndPos);
		const float Cc = Na.Dot(Nb);
		const float Den = 1.f - Cc * Cc;
		if (std::fabs(Den) > 1e-4f)
		{
			const TFVec3 OnLine = (Na * (Da - Db * Cc) + Nb * (Db - Da * Cc)) / Den;
			const TFVec3 Ln = Line.Normalized();
			const TFVec3 Cand = OnLine + Ln * Ln.Dot(Mid - OnLine);
			if ((Cand - A.EndPos).LengthSq() <= kGrappleCornerRadiusSq && (Cand - B.EndPos).LengthSq() <= kGrappleCornerRadiusSq)
			{
				Corner = Cand;
			}
		}
	}
	Corner += (Na + Nb).Normalized() * 1.f; // keep the corner off the surface [I]
	for (int I = G.PointCount; I > 0; --I) { G.Points[I] = G.Points[I - 1]; }
	G.Points[0] = Corner;
	G.PointCount++;
}

// -------------------------------------------------------------------------------------------
// Zipline (grapple_zipline.md sec.2)
// -------------------------------------------------------------------------------------------
struct TFRopeCurve { float A = 0.f, B = 0.f, C = 0.f; };

TFRopeCurve RopeSegmentCurve(const TFZiplineRope& Rope, int Seg)
{
	TFRopeCurve Out;
	const float Z0 = Rope.Nodes[Seg].Z;
	const float Z1 = Rope.Nodes[Seg + 1].Z;
	if (static_cast<int>(Rope.SegmentMidZ.size()) > Seg)
	{
		// Fit z(t) = A t^2 + B t + C through t = 0, 0.5, 1.
		const float Zm = Rope.SegmentMidZ[Seg];
		Out.A = 2.f * (Z0 + Z1) - 4.f * Zm;
		Out.B = Z1 - Z0 - Out.A;
	}
	else
	{
		Out.B = Z1 - Z0;
	}
	Out.C = Z0;
	return Out;
}

int RopeSegmentCount(const TFZiplineRope& Rope) { return static_cast<int>(Rope.Nodes.size()) - 1; }

struct TFZipStep { TFVec3 Pos; TFVec3 Vel; float T = 0.f; bool bValid = false; };

// Zipline_AdvanceAlongSegment: chord speed integrator + sag-curve height.
TFZipStep ZiplineAdvance(TFMoveCtx& C, const TFVec3& A, const TFVec3& B, bool bReverse, float Accel, float MaxSpeed,
	const TFRopeCurve& Curve, const TFVec3& Pos, const TFVec3& Vel, float Dt)
{
	TFZipStep Out;
	const TFVec3 AB = B - A;
	const float Ab2 = AB.Length2DSq();
	if (Ab2 < 1e-4f) { return Out; } // vertical segments are not supported
	const TFVec3 Dir = AB.Normalized();
	const float T0 = std::max(0.f, (Pos - A).Dot2D(AB) / Ab2);
	TFVec3 V = Vel;
	// Gravity on a descending chord only; as written it pushes vz UP, i.e. slows the rider [R] (Z3).
	float G = C.P.GravityScale * C.CV.sv_gravity * 0.5f * Dt * Dir.Z;
	if (G > 0.f) { G = -G; }
	const bool bDescending = bReverse ? (Dir.Z > 0.f) : (Dir.Z < 0.f);
	if (!bDescending && Dir.Z != 0.f) { G = 0.f; }
	V.Z -= G;
	float Sp = Dir.Dot(V) + (bReverse ? -Accel * Dt : Accel * Dt);
	if (bReverse ? Sp > 0.f : Sp < 0.f) { Sp = 0.f; } // never runs backwards
	Sp = Clamp(Sp, -MaxSpeed, MaxSpeed);
	Out.Vel = Dir * Sp;
	const TFVec3 P = A + AB * T0 + Out.Vel * Dt;
	const float T = Clamp01((P - A).Dot2D(AB) / Ab2);
	Out.Pos = TFVec3(LerpF(A.X, B.X, T), LerpF(A.Y, B.Y, T), (Curve.A * T + Curve.B) * T + Curve.C);
	Out.T = T;
	Out.bValid = true;
	return Out;
}

void ZiplineDetach(TFMoveCtx& C)
{
	TFZiplineState& Z = C.S.Zipline;
	if (Z.State == 0) { return; }
	Z.LastRopeId = Z.RopeId;
	Z.RopeId = -1;
	Z.LastCooldown = C.P.UseZiplineCooldown;
	Z.State = 0;
	Z.DetachTime = C.Now;
	C.S.bOnGround = false;
	Event(C, ETFMoveEvent::ZiplineDetach);
}

bool ZiplineCanUse(const TFMoveCtx& C)
{
	return C.In.bAlive && !C.S.Mantle.bActive;
}

// Zipline_Find + attach. Simplified [I]: the 200-unit anchor trims are replaced by the final
// hull trace to the attach point; the script veto is not modelled.
void ZiplineTryAttach(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (S.Zipline.State != 0 || !ZiplineCanUse(C) || S.Grapple.bPulling) { return; }
	const bool bTrigger = C.In.bUsePressed || (C.P.bZiplineAutoAttachInAir && !S.bOnGround && !S.bWallRunning);
	if (!bTrigger) { return; }

	const float Range = C.CV.zipline_use_range;
	const TFVec3 Eye = EyePos(C);
	const TFZiplineRope* Ropes[16];
	const int NumRopes = C.W.QueryZiplines(Eye, Range + 10.f, Ropes, 16);

	const TFZiplineRope* Best = nullptr;
	int BestSeg = -1;
	float BestScore = FLT_MAX;
	TFVec3 BestPoint;
	for (int R = 0; R < NumRopes; ++R)
	{
		const TFZiplineRope* Rope = Ropes[R];
		if (!Rope || Rope->Nodes.size() < 2) { continue; }
		if (Rope->Id == S.Zipline.LastRopeId && S.Zipline.LastCooldown > 0.f) { continue; }
		for (int Seg = 0; Seg < RopeSegmentCount(*Rope); ++Seg)
		{
			const TFVec3 A = Rope->Nodes[Seg];
			const TFVec3 B = Rope->Nodes[Seg + 1];
			const TFVec3 AB = B - A;
			const float Ab2 = AB.LengthSq();
			if (Ab2 < 0.01f) { continue; }
			// Horizontal distance to the rope's XY line.
			const float Ab2xy = AB.Length2DSq();
			if (Ab2xy > 1e-4f)
			{
				const float Uxy = Clamp01((Eye - A).Dot2D(AB) / Ab2xy);
				const TFVec3 Cxy = A + AB * Uxy;
				if ((Eye - Cxy).Length2DSq() >= Range * Range) { continue; }
			}
			// Closest 3D point within range.
			const float U = Clamp01((Eye - A).Dot(AB) / Ab2);
			if (((A + AB * U) - Eye).LengthSq() > Range * Range) { continue; }
			// Clip the segment to the sphere of radius range + 10.
			const float Rr = Range + 10.f;
			const TFVec3 F = A - Eye;
			const float Qa = Ab2, Qb = 2.f * F.Dot(AB), Qc = F.LengthSq() - Rr * Rr;
			const float Disc = Qb * Qb - 4.f * Qa * Qc;
			if (Disc < 0.f) { continue; }
			const float SqD = std::sqrt(Disc);
			const float U0 = std::max(0.f, (-Qb - SqD) / (2.f * Qa));
			const float U1 = std::min(1.f, (-Qb + SqD) / (2.f * Qa));
			if (U0 > U1) { continue; }
			// Point of the clipped segment closest to the view ray.
			const TFVec3 Dv = C.ViewFwd;
			const float Bb = AB.Dot(Dv);
			const float Den = Ab2 - Bb * Bb;
			float Ub = U;
			if (std::fabs(Den) > 1e-4f) { Ub = (Bb * Dv.Dot(F) - F.Dot(AB)) / Den; }
			Ub = Clamp(Ub, U0, U1);
			const TFVec3 Pt = A + AB * Ub;
			const TFVec3 ToPt = Pt - Eye;
			const float Along = std::max(0.f, ToPt.Dot(Dv));
			const float Score = (ToPt - Dv * Along).LengthSq();
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Rope;
				BestSeg = Seg;
				BestPoint = Pt;
			}
		}
	}
	if (!Best) { return; }

	const TFVec3 A = Best->Nodes[BestSeg];
	const TFVec3 B = Best->Nodes[BestSeg + 1];
	const TFVec3 AB = B - A;
	const float Ab2xy = AB.Length2DSq();
	if (Ab2xy < 1e-4f) { return; }
	const float T = Clamp01((BestPoint - A).Dot2D(AB) / Ab2xy);
	const TFRopeCurve Curve = RopeSegmentCurve(*Best, BestSeg);
	const TFVec3 Attach(LerpF(A.X, B.X, T), LerpF(A.Y, B.Y, T), (Curve.A * T + Curve.B) * T + Curve.C);

	bool bReverse = AB.Normalized().Dot(C.ViewFwd) < 0.f;
	const int NumSeg = RopeSegmentCount(*Best);
	const float EndDist = std::max(Best->AutoDetachDistance, 0.01f);
	if (BestSeg == 0 && (Attach - Best->Nodes.front()).Length() <= EndDist) { bReverse = false; }
	if (BestSeg == NumSeg - 1 && (Attach - Best->Nodes.back()).Length() <= EndDist) { bReverse = true; }

	const TFVec3 Hang = Attach - TFVec3(0.f, 0.f, HullHeight(C));
	if (Sweep(C, S.Origin, Hang).Hit()) { return; }

	TFZiplineState& Z = S.Zipline;
	Z.State = 1;
	Z.RopeId = Best->Id;
	Z.Segment = BestSeg;
	Z.bReverse = bReverse;
	Z.AttachPoint = Attach;
	EndSlide(C);
	if (S.bWallRunning) { StopWallRun(C); }
	S.bOnGround = false;
	Event(C, ETFMoveEvent::ZiplineAttach);
}

void ZiplineMove(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	TFZiplineState& Z = S.Zipline;
	const TFPilotSettings& P = C.P;
	const TFZiplineRope* Rope = C.W.GetZipline(Z.RopeId);
	if (!Rope || Rope->Nodes.size() < 2 || !ZiplineCanUse(C)) { ZiplineDetach(C); return; }

	// Dismount (crouch drops, jump launches along the view). Damp 1 leaves velocity unchanged.
	if (C.In.bCrouchPressed || C.bJumpPressed)
	{
		const TFVec3 T = C.In.bCrouchPressed ? TFVec3(0.f, 0.f, -P.ZiplineJumpOffSpeed) : C.ViewFwd * P.ZiplineJumpOffSpeed;
		S.Velocity = T + (S.Velocity - T) * P.ZiplineJumpOffDampScale;
		ZiplineDetach(C);
		return;
	}

	const float HullH = HullHeight(C);
	const float MaxSpeed = Rope->MoveSpeedScale * P.ZiplineSpeed;
	const int NumSeg = RopeSegmentCount(*Rope);
	Z.Segment = std::max(0, std::min(Z.Segment, NumSeg - 1));

	if (Z.State == 1)
	{
		// Mount start: duration is the viewmodel mount animation length in the game [?];
		// mountZiplineTime is the documented fallback.
		Z.MountDuration = P.MountZiplineTime;
		Z.MountAlpha = 0.f;
		Z.MountStartPos = S.Origin;
		Z.MountEntryVel = S.Velocity;
		Z.State = 2;
		return;
	}

	const TFVec3 A = Rope->Nodes[Z.Segment];
	const TFVec3 B = Rope->Nodes[Z.Segment + 1];
	if ((B - A).Length() < 0.1f) { ZiplineDetach(C); return; }
	const TFVec3 Dir = (B - A).Normalized();
	const TFRopeCurve Curve = RopeSegmentCurve(*Rope, Z.Segment);
	const int NextSeg = Z.bReverse ? Z.Segment - 1 : Z.Segment + 1;
	const bool bHasNext = NextSeg >= 0 && NextSeg < NumSeg;

	if (Z.State == 2)
	{
		// Mount blend: the attach point slides at b*0.5*maxSpeed, the player lerps onto it and the
		// velocity lerps to exactly ziplineSpeed along the chord (no rope scale).
		const float Bl = Clamp01(Z.MountAlpha + C.Dt / std::max(0.01f, Z.MountDuration));
		const float Along = Bl * 0.5f * MaxSpeed;
		const TFVec3 AttachVel = Dir * (Z.bReverse ? -Along : Along);
		const TFZipStep Step = ZiplineAdvance(C, A, B, Z.bReverse, 0.f, MaxSpeed, Curve, Z.AttachPoint, AttachVel, C.Dt);
		if (!Step.bValid) { ZiplineDetach(C); return; }
		const TFVec3 OldHang = Z.AttachPoint - TFVec3(0.f, 0.f, HullH);
		const TFVec3 NewHang = Step.Pos - TFVec3(0.f, 0.f, HullH);
		const TFVec3 NewPos = TFVec3::Lerp(Z.MountStartPos, NewHang, Bl);
		const bool bBlockedA = Sweep(C, S.Origin, NewPos).Hit();
		const bool bBlockedB = Sweep(C, OldHang, NewHang).Hit();
		if (bBlockedA && bBlockedB) { ZiplineDetach(C); return; }
		S.Origin = NewPos;
		S.Velocity = TFVec3::Lerp(Z.MountEntryVel, Dir * (Z.bReverse ? -P.ZiplineSpeed : P.ZiplineSpeed), Bl);
		Z.AttachPoint = Step.Pos;
		Z.MountAlpha = Bl;
		if (bHasNext && (Z.bReverse ? Step.T <= 0.f : Step.T >= 1.f)) { Z.Segment = NextSeg; }
		if (Bl >= 1.f) { Z.State = 3; }
		CheckVelocity(C);
		return;
	}

	// State 3: ride.
	const TFVec3 RopePos = S.Origin + TFVec3(0.f, 0.f, HullH);
	const TFZipStep Step = ZiplineAdvance(C, A, B, Z.bReverse, P.ZiplineAcceleration, MaxSpeed, Curve, RopePos, S.Velocity, C.Dt);
	if (!Step.bValid) { ZiplineDetach(C); return; }
	const TFVec3 NewPos = Step.Pos - TFVec3(0.f, 0.f, HullH);
	if (Sweep(C, S.Origin, NewPos).Hit()) { ZiplineDetach(C); return; }
	S.Origin = NewPos;
	S.Velocity = Step.Vel;
	Z.AttachPoint = Step.Pos;

	const bool bAtEnd = Z.bReverse ? Step.T <= 0.f : Step.T >= 1.f;
	if (bHasNext)
	{
		if (bAtEnd) { Z.Segment = NextSeg; } // bends keep s*cos(theta) via the projection next tick
	}
	else
	{
		const TFVec3 EndNode = Z.bReverse ? Rope->Nodes.front() : Rope->Nodes.back();
		if (bAtEnd || (Step.Pos - EndNode).Length() < std::max(0.01f, Rope->AutoDetachDistance))
		{
			ZiplineDetach(C);
		}
	}
	CheckVelocity(C);
}

// -------------------------------------------------------------------------------------------
// Auto-mantle (slide_mantle.md sec.2)
// -------------------------------------------------------------------------------------------
bool MantleAngleOK(TFMoveCtx& C, const TFVec3& WallN)
{
	const TFVec3 Wn = WallN.Normalized2D();
	const float Push = -Wn.Dot(C.WishDir);
	if (Push < 0.f) { return false; }
	if (C.WishDir.LengthSq() * Sq(std::cos(C.CV.automantle_maxangle_push * kDegToRad)) > Push * Push) { return false; }
	const float View = -Wn.Dot(C.YawFwd) * C.P.MantleAngleScale;
	if (!C.S.bWallRunning)
	{
		return View >= std::cos(C.CV.automantle_maxangle_view * kDegToRad);
	}
	return View >= std::cos(C.CV.automantle_wallrun_maxangle_view * kDegToRad)
		&& C.S.Velocity.Dot(Wn) >= C.P.MantleWallrunIntoWallLimit;
}

// FindMantleLedge: 0 = reject, 1 = no ledge, 2 = ledge found.
int FindMantleLedge(TFMoveCtx& C, const TFTraceResult& WallTr, TFVec3& OutLedge, TFVec3& OutWallN, float& OutDist, bool& OutDangle)
{
	TFPilotState& S = C.S;
	const TFMovementConVars& CV = C.CV;
	const TFPilotSettings& P = C.P;
	const TFVec3 Pos = S.Origin;
	OutDangle = false;
	const TFVec3 WallN = WallTr.Normal.Normalized2D();
	if (WallN.IsNearlyZero()) { return 0; }
	OutWallN = WallN;
	const float Wd = (Pos - WallTr.EndPos).Dot(WallN);
	if (!MantleAngleOK(C, WallN)) { return 0; }

	const float Reach = CV.automantle_forwarddist + Wd;
	const float D = std::max(CV.automantle_forwarddist, CV.automantle_mindist) + Wd;
	const float Top = CV.automantle_height + P.MantleEpsilon;
	const uint32_t Slot = S.TickCount % 5u; // probe height cycles per command
	const float ProbeOff = (Slot == 0) ? Top : kMantleProbeHeights[Slot];
	const TFVec3 Start(Pos.X, Pos.Y, Pos.Z + ProbeOff);
	const TFVec3 End = Start - WallN * D;
	const TFTraceResult T = C.W.TraceLine(Start, End);
	const float Frac = T.Hit() ? T.Fraction : 1.f;
	const float Pen = Frac * D;
	if (Pen - Wd < CV.automantle_mindist) { return 1; }

	const TFVec3 Pt = TFVec3::Lerp(Start, End, std::min(Frac, Reach / D));
	const TFTraceResult LedgeTr = C.W.TraceLine(Pt, Pt - TFVec3(0.f, 0.f, Top));
	if (!LedgeTr.Hit() || LedgeTr.Normal.Z < P.WalkableNormalZ) { return 1; }
	const float Lh = LedgeTr.EndPos.Z - Pos.Z;
	if (Lh < P.MantleEpsilon || LedgeTr.bStartSolid) { return 0; }
	if (C.W.TraceLine(Pos, Start).Hit()) { return 0; } // headroom

	const bool bInto = S.Velocity.Dot(WallN) < -1.f;
	if (Lh < P.StepHeight + P.MantleEpsilon && !S.bWallRunning && bInto)
	{
		// Low ledge: let normal stepping handle it if the step test is clear.
		if (S.DuckState == ETFDuckState::Ducked) { return 0; }
		const TFVec3 StepPos = Pos + TFVec3(0.f, 0.f, Lh + P.MantleEpsilon);
		if (!Sweep(C, StepPos, StepPos).bStartSolid) { return 0; }
	}
	if (Lh < 2.f * Pen && bInto && (std::fabs(WallTr.Normal.Z) > 0.1f || Lh < 2.f * P.StepHeight))
	{
		const TFVec3 Mid(Pos.X, Pos.Y, Pos.Z + Lh * 0.5f);
		if (!C.W.TraceLine(Mid, Mid - WallN * (Pen * 0.35f)).Hit()) { return 0; } // not a real wall
	}
	const TFVec3 Q(Pos.X, Pos.Y, LedgeTr.EndPos.Z - (CV.automantle_dangle_required_space - 5.f));
	OutDangle = C.W.TraceLine(Q, Q - WallN * (2.f * P.HullRadius)).Hit();
	OutDist = std::min(Reach, Pen);
	OutLedge = LedgeTr.EndPos;
	return 2;
}

void StopTraversal(TFMoveCtx& C)
{
	TFMantleState& M = C.S.Mantle;
	if (!M.bActive) { return; }
	M.bActive = false;
	M.ReleaseTime = C.Now;
	M.bWeaponDisabled = false;
	Event(C, ETFMoveEvent::MantleEnd);
}

bool CheckAutoMantle(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const TFMovementConVars& CV = C.CV;
	const TFPilotSettings& P = C.P;
	TFMantleState& M = S.Mantle;

	if (!CV.automantle_enable || !P.bAutomantleEnabled || !C.In.bAlive) { return false; }
	if (C.Now < M.ReleaseTime + CV.traversal_cooldown) { return false; }
	const bool bJumpHeld = C.In.bJumpHeld;
	if (S.bOnGround && !S.bWallRunning && !bJumpHeld) { return false; }
	if (C.Now - S.OnSlopeTime < P.MantleSlopeGateTime && !bJumpHeld) { return false; }
	if (S.Grapple.bActive && S.Grapple.bAttached) { return false; }
	if (C.Now - M.StartTime < CV.automantle_cooldown) { return false; }

	const TFVec3 Fwd = C.YawFwd;
	const float FwdSpd = Fwd.Dot2D(S.Velocity);
	const bool bPressingFwd = C.ForwardMove >= P.MantlePressingForward;
	if (!bPressingFwd && !S.bWallRunning && FwdSpd <= 0.01f) { return false; }

	float Dist = std::max(CV.automantle_searchdist - 0.5f, 0.5f);
	Dist = std::max(Dist, std::max(FwdSpd, 0.f) * C.Dt);
	TFVec3 End = S.Origin + Fwd * Dist;
	TFTraceResult Tr = Sweep(C, S.Origin, End);
	if (Tr.Hit() && Tr.Normal.Z > P.WalkableNormalZ)
	{
		End.Z -= Fwd.Dot(Tr.Normal) / Tr.Normal.Z * Dist;
		Tr = Sweep(C, S.Origin, End);
	}
	if (!Tr.Hit() || Tr.bStartSolid || Tr.Normal.Z >= P.WalkableNormalZ)
	{
		M.bWeaponDisabled = false; // anticipation only lowers the weapon early; never starts a mantle
		return false;
	}

	TFVec3 Ledge, WallN;
	float D = 0.f;
	bool bDangle = false;
	const int R = FindMantleLedge(C, Tr, Ledge, WallN, D, bDangle);
	if (R != 2 || !(bDangle || bPressingFwd)) { return false; }

	// Ledge-top settle (simplified): lift until the hull fits above the ledge.
	TFVec3 TopPos = Ledge + TFVec3(0.f, 0.f, P.MantleEpsilon);
	bool bCrouch = S.bCrouchedHull;
	bool bFits = false;
	for (int I = 0; I < 2 && !bFits; ++I)
	{
		const TFVec3 Test = TopPos + TFVec3(0.f, 0.f, kMantleSettleStep * static_cast<float>(I));
		if (!bCrouch && !C.W.SweepHull(Test + WallN * (0.99f * D), Test, false).Hit()) { bFits = true; TopPos = Test; }
		else if (!C.W.SweepHull(Test + WallN * (0.99f * D), Test, true).Hit()) { bFits = true; bCrouch = true; TopPos = Test; }
	}
	if (!bFits) { return false; } // [I] no room even crouched

	const float H = CV.automantle_height;
	TFVec3 Begin = S.Origin;
	if (S.Origin.Z < TopPos.Z - (H + P.MantleEpsilon)) { Begin.Z = TopPos.Z - (H + P.MantleEpsilon); }
	// horiz = min(frac*d, d - mindist) + hullR; the ledge probe passed (frac 1), so d - mindist [I].
	const float Horiz = std::max(D - CV.automantle_mindist, 0.f) + P.HullRadius;
	const float MidFrac = H / (H + std::max(Horiz, 1.f));

	// Wall below the ledge: fraction of a downward sweep along the face [I] (the source of "frac"
	// in the climb floor and may-hang rules is not named in the docs).
	const TFVec3 FaceTop(Begin.X, Begin.Y, TopPos.Z);
	const TFTraceResult DownTr = C.W.SweepHull(FaceTop, FaceTop - TFVec3(0.f, 0.f, H), true);
	const float DownFrac = DownTr.bStartSolid ? 0.f : (DownTr.Hit() ? DownTr.Fraction : 1.f);

	if (bCrouch && !S.bCrouchedHull) { ForceDuck(C); }
	M = TFMantleState();
	M.bActive = true;
	M.bFirstTick = true;
	M.bCrouched = bCrouch;
	M.WallNormal = WallN;
	M.ForwardDir = -WallN;
	M.Begin = Begin;
	M.Mid = TFVec3(Begin.X, Begin.Y, TopPos.Z);
	M.End = TopPos;
	M.MidFrac = MidFrac;
	M.StartTime = C.Now;
	M.ReleaseTime = 0.f;
	M.bMayHang = bDangle && DownFrac * H >= CV.automantle_dangle_required_space;
	M.Progress = std::min(1.f, (Begin.Z - (TopPos.Z - (H + P.MantleEpsilon))) / H) * MidFrac;
	M.ClimbProgressFloor = (1.f - DownFrac) * MidFrac;
	S.Velocity = TFVec3();
	if (S.bWallRunning) { StopWallRun(C); }
	EndSlide(C);
	S.bOnGround = false;
	Event(C, ETFMoveEvent::MantleStart, MidFrac);
	return true;
}

TFVec3 MantlePathPos(const TFMantleState& M, float Prog, float H)
{
	// traversal_anim 1 follows the first-person-proxy animation root motion (model data, [?]).
	// We use the documented traversal_anim 0 reading: vertical begin->mid, then horizontal mid->end [I].
	if (Prog < M.MidFrac)
	{
		const float K = M.MidFrac > 0.f ? Prog / M.MidFrac : 1.f;
		return TFVec3(M.Begin.X, M.Begin.Y, LerpF(M.End.Z - H, M.End.Z, K));
	}
	const float K = (M.MidFrac < 1.f) ? (Prog - M.MidFrac) / (1.f - M.MidFrac) : 1.f;
	return TFVec3::Lerp(M.Mid, M.End, Clamp01(K));
}

void TraversalMove(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	TFMantleState& M = S.Mantle;
	const TFMovementConVars& CV = C.CV;
	const TFPilotSettings& P = C.P;
	const bool bFirst = M.bFirstTick;
	M.bFirstTick = false;
	S.Velocity = TFVec3();

	const float H = CV.automantle_height;
	const float EyeZ = M.bCrouched ? P.CrouchEyeHeight : P.StandEyeHeight;

	if (bFirst)
	{
		// Classification by eye height relative to the ledge.
		const float Hh = (EyeZ + S.Origin.Z) - M.End.Z;
		const TFVec3 Eye(S.Origin.X, S.Origin.Y, S.Origin.Z + EyeZ);
		if (Hh >= CV.automantle_height_below || (Eye - M.End).Dot(M.ForwardDir) <= -P.HullRadius - 4.f)
		{
			M.BaseClass = (Hh >= CV.automantle_height_level)
				? ((Hh >= CV.automantle_height_above) ? ETFMantleClass::High : ETFMantleClass::Above)
				: ETFMantleClass::Level;
		}
		else
		{
			M.BaseClass = ETFMantleClass::Below;
		}
	}

	const bool bBelow = M.BaseClass == ETFMantleClass::Below;
	const float Rest = bBelow ? CV.automantle_rest_frac_below : CV.automantle_rest_frac;
	const float Mid = M.MidFrac;
	const float Dp = M.ForwardDir.Dot(C.WishDir);
	const bool bPushing = Dp > P.MantlePushDot;
	const bool bPulling = Dp < P.MantlePullDot;
	if (bPushing) { M.bLastPushedForward = true; }
	else if (bPulling) { M.bLastPushedForward = false; }
	float Prog = M.Progress;

	if (C.bJumpPressed && !M.bForceFallOff && (!bPushing || Mid <= Prog) && M.JumpOffTime == 0.f)
	{
		M.JumpOffTime = C.Now;
	}
	const bool bIdleInput = M.JumpOffTime == 0.f;
	bool bAbort = bIdleInput && bPulling;
	bool bAdvance = bIdleInput && bPushing;
	if (Rest < Prog && !bAdvance && !bAbort && !C.bJumpPressed) { bAdvance = (Mid < Prog) ? true : M.bLastPushedForward; }
	if (M.bForceFallOff) { bAdvance = false; bAbort = true; }

	float Dur = CV.automantle_duration_below;
	float Thr = -H;
	switch (M.BaseClass)
	{
	case ETFMantleClass::Level: Dur = CV.automantle_duration_level; Thr = CV.automantle_height_below - EyeZ; break;
	case ETFMantleClass::Above: Dur = CV.automantle_duration_above; Thr = CV.automantle_height_level - EyeZ; break;
	case ETFMantleClass::High: Dur = CV.automantle_duration_high; Thr = CV.automantle_height_above - EyeZ; break;
	default: break;
	}
	const float StartFrac = Clamp(((Thr + H) / H) * Mid, 0.f, 0.999f);
	const float Rate = (1.f - StartFrac) / std::max(Dur, 1e-3f);

	float Target = 1.f;
	bool bAtRest = false;
	if (M.JumpOffTime == 0.f)
	{
		if (!bAdvance) { Target = (bAbort || !M.bMayHang) ? 0.f : Rest; }
		Target = std::max(Target, M.ClimbProgressFloor);
		Prog = Approach(Prog, Target, Rate * C.Dt);
		// The code snaps to automantle_rest_frac even for BELOW (M3, likely an artefact). We keep the
		// class rest fraction so a BELOW hang does not oscillate [I].
		if (Prog == Target && Target == Rest) { bAtRest = true; }
	}

	const float GunFrac = (CV.automantle_gun_enable_height - EyeZ + H) / (H + P.HullRadius);
	M.bWeaponDisabled = !(GunFrac <= Target && GunFrac <= Prog);
	M.AnimClass = M.JumpOffTime != 0.f ? ETFMantleClass::JumpOff : (bAtRest ? ETFMantleClass::Idle : M.BaseClass);
	const float Cycle = Clamp01((Prog - StartFrac) / (1.f - StartFrac));

	if (Prog >= 1.f)
	{
		M.Progress = 1.f;
		S.Origin = M.End;
		StopTraversal(C);
		SetGround(C, true, TFVec3::Up());
		S.OnSlopeTime = C.Now;
		S.TimeLastTouchedGround = C.Now;
		if (bPushing) { S.Velocity = C.WishDir * StanceSpeed(C, 0); }
		return;
	}

	const TFVec3 NewPos = MantlePathPos(M, Prog, H);
	if (C.W.SweepHull(NewPos, NewPos, S.bCrouchedHull).bStartSolid)
	{
		StopTraversal(C); // TraversalInvalid [I]
		return;
	}
	S.Origin = NewPos;

	if (M.JumpOffTime != 0.f && (C.Now >= M.JumpOffTime + CV.automantle_jumpoff_duration || Prog > CV.automantle_jumpoff_anim_maxfrac))
	{
		StopTraversal(C);
		S.bHasJumpedSinceTouchedGround = true;
		if (Prog >= Mid)
		{
			// Normal jump from the top [I: treated as a ground jump].
			S.Velocity = C.WishDir * StanceSpeed(C, 0);
			CheckJumpButton(C, true);
		}
		else
		{
			// Kick off the wall with the wall-jump speeds (settings 0x228/0x230).
			S.Velocity = M.ForwardDir * -P.WallrunJumpOutwardSpeed;
			S.Velocity.Z = P.WallrunJumpUpSpeed;
		}
		M.Progress = Prog;
		return;
	}

	const bool bCrouchInput = C.In.bCrouchPressed || C.In.bCrouchHeld;
	if ((bAbort && (Prog >= Mid || Prog >= CV.automantle_backoff_anim_maxfrac))
		|| ((Prog <= M.ClimbProgressFloor || (Target == 0.f && Cycle <= 0.f)) && (!bPushing || M.bForceFallOff) && !bFirst))
	{
		StopTraversal(C);
		return;
	}
	if (bAbort || (M.AnimClass == ETFMantleClass::Idle && bCrouchInput))
	{
		if (M.AnimClass == ETFMantleClass::Idle) { M.bForceFallOff = true; }
		else if (!M.bForceFallOff) { StopTraversal(C); return; }
	}
	M.Progress = Prog;
}

// -------------------------------------------------------------------------------------------
// Air move, walk move, wallrun move
// -------------------------------------------------------------------------------------------
void ProcessMoveHits(TFMoveCtx& C, const TFMoveHits& Hits)
{
	for (int I = 0; I < Hits.Num; ++I)
	{
		const TFHitRecord& H = Hits.Hits[I];
		if (H.Normal.Z < C.P.WalkableNormalZ) { C.S.LastWallNormal = H.Normal; }
		TryStartWallRun(C, H.EndPos, H.Normal, H.VelBeforeClip);
	}
}

void AirMove(TFMoveCtx& C, float Dt)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	const TFMovementConVars& CV = C.CV;

	TFVec3 W = C.WishDir;
	float Mag = W.Length();
	const TFVec3 WishDir = Mag > 1e-6f ? W / Mag : TFVec3();
	Mag = std::min(Mag, 1.f);
	float Spd;
	float Acc;
	if (S.Grapple.bActive && S.Grapple.bPulling)
	{
		Spd = P.GrappleAirSpeedMax;
		Acc = P.GrappleAirAccel;
	}
	else
	{
		Spd = P.bZeroAirSpeed ? 0.f : P.AirSpeed;
		Acc = P.AirAcceleration;
	}
	const float WishSpeed = SpeedScale(C) * MaxSpeedScale(C) * Mag * Spd;
	AirAccelerate(C, WishDir, WishSpeed, Acc * P.AccelScale, Dt);

	// Keyboard grace: a move key pressed within 0.5 s after a jump rotates horizontal velocity.
	const bool bKeyPressed = C.bFwdPressed || C.bBackPressed || C.bLeftPressed || C.bRightPressed;
	const float DtJ = C.Now - S.TimeLastJumped;
	if (bKeyPressed && DtJ < CV.jump_keyboardgraceperiodmax)
	{
		const float Kf = (C.bFwdHeld ? 1.f : 0.f) - (C.bBackHeld ? 1.f : 0.f);
		const float Ks = (C.bRightHeld ? 1.f : 0.f) - (C.bLeftHeld ? 1.f : 0.f);
		const TFVec3 KDir = (C.YawFwd * Kf + C.YawRight * Ks).Normalized2D();
		const float Hs = S.Velocity.Length2D();
		if (Hs > 1e-3f && !KDir.IsNearlyZero())
		{
			const TFVec3 VDir(S.Velocity.X / Hs, S.Velocity.Y / Hs, 0.f);
			const float Den = CV.jump_keyboardgraceperiodmin - CV.jump_keyboardgraceperiodmax;
			const float Wt = Clamp01(Den != 0.f ? (DtJ - CV.jump_keyboardgraceperiodmax) / Den : 1.f) * CV.jump_keyboardgrace_strength;
			TFVec3 Nd = TFVec3::Lerp(VDir, KDir, Wt);
			if (Nd.Length2D() > 0.01f)
			{
				Nd = Nd.Normalized2D() * Hs;
				TFVec3 Dv(Nd.X - S.Velocity.X, Nd.Y - S.Velocity.Y, 0.f);
				const float Cap = StanceSprintSpeed(C, 0) * CV.jump_keyboardgrace_max;
				const float Dl = Dv.Length2D();
				if (Dl > Cap && Dl > 0.f) { Dv *= Cap / Dl; }
				S.Velocity.X += Dv.X;
				S.Velocity.Y += Dv.Y;
			}
		}
	}

	S.Velocity += S.PushAwayFromTopDir * (S.PushAwayFromTopAccel * Dt);
	S.LastWallNormal = TFVec3();

	if (C.P.bApplyWallRepel) { ApplyWallRepel(C, S.Velocity, WishDir); }

	// Wallrun retry after a near miss.
	if (S.WallrunRetryTime != 0.f && C.Now >= S.WallrunRetryTime
		&& std::fabs((S.Origin - S.WallrunRetryPos).Dot(S.WallrunRetryNormal)) < 1.f)
	{
		const TFVec3 N = S.WallrunRetryNormal;
		S.WallrunRetryTime = 0.f;
		const TFTraceResult Tr = Sweep(C, S.Origin, S.Origin - N * C.CV.wallrun_allowed_wall_dist);
		if (Tr.Hit() && !Tr.bStartSolid) { TryStartWallRun(C, Tr.EndPos, Tr.Normal, S.Velocity); }
	}

	TFMoveHits Hits;
	TFVec3 Pos = S.Origin;
	TFVec3 Vel = S.Velocity;
	SlideMove(C, Pos, Vel, Dt, &Hits);
	S.Origin = Pos;
	S.Velocity = Vel;
	if (!S.bWallRunning) { ProcessMoveHits(C, Hits); }
}

void WalkMoveGround(TFMoveCtx& C, float Dt)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;

	S.Velocity -= S.UpDir * S.Velocity.Dot(S.UpDir);
	S.FallVelocity = 0.f;

	float F = C.ForwardMove;
	float Sm = C.SideMove;
	if (S.bSprinting && P.SprintInputDivisor > 0.f) { F /= P.SprintInputDivisor; Sm /= P.SprintInputDivisor; }
	float Lim = 1.f;
	if (S.DuckState == ETFDuckState::Ducking || S.DuckState == ETFDuckState::Ducked)
	{
		const float A = StanceSpeed(C, 1);
		const float B = StanceSpeed(C, 0);
		Lim = (std::fabs(B) <= 0.001f) ? (A == 0.f ? 0.f : 1.f) : A / B;
	}
	const float M2 = F * F + Sm * Sm;
	float Scale = 0.f;
	if (M2 > Lim) { Scale = Lim / std::sqrt(M2); }           // compares m^2 with lim, as in the code
	else if (M2 > 0.f) { const float Mg = std::sqrt(M2); Scale = (2.f - Mg) * Mg * Lim / Mg; } // analogue ease
	F *= Scale;
	Sm *= Scale;
	Sm *= P.SpeedScaleSide;
	if (F < 0.f) { F *= P.SpeedScaleBack; }

	TFVec3 Wish = C.YawFwd * F + C.YawRight * Sm;
	Wish -= S.UpDir * Wish.Dot(S.UpDir);
	if (S.bSliding)
	{
		// Input along the current velocity is removed: steer, but no forward accel.
		const TFVec3 Vd = S.Velocity.Normalized();
		const float Dd = Wish.Dot(Vd);
		if (Dd > 0.f || (Dd < 0.f && S.CrouchMode == 3)) { Wish -= Vd * Dd; }
	}
	const float WishLen = Wish.Length();
	const float WishSpeed = std::min(WishLen, 1.f) * S.MaxSpeed;
	const TFVec3 WishDir = WishLen > 1e-6f ? Wish / WishLen : TFVec3();

	const float Accel = GroundAccel(C);
	Friction(C, WishDir, WishSpeed, Dt);
	Accelerate(C, WishDir, WishSpeed, Accel, Dt);

	if (S.bSliding && S.Velocity.Length() < P.SlideStopSpeed && S.CrouchMode != 3) { EndSlide(C); }

	if (S.Velocity.Length() >= 1.f)
	{
		// Re-project onto the slope for the move, keeping |vel| [R]; the stored velocity stays
		// horizontal when nothing is hit [I] (avoids a per-tick cos loss after the up-strip).
		const TFVec3 Hv = S.Velocity;
		const TFVec3& Gn = S.GroundNormal;
		bool bProjected = false;
		if (Gn.Z < 0.9999f && Gn.Z > 0.f)
		{
			const float Spd = S.Velocity.Length();
			S.Velocity = (S.Velocity - Gn * S.Velocity.Dot(Gn)).Normalized() * Spd;
			bProjected = true;
		}
		S.LastWallNormal = TFVec3();
		TFMoveHits Hits;
		StepMove(C, Dt, Hits);
		if (bProjected && Hits.Num == 0) { S.Velocity = Hv; }
		for (int I = 0; I < Hits.Num; ++I)
		{
			const TFHitRecord& H = Hits.Hits[I];
			if (H.Normal.Z < P.WalkableNormalZ)
			{
				S.LastWallNormal = H.Normal;
				TryStartWallRun(C, H.EndPos, H.Normal, H.VelBeforeClip);
			}
		}
		StayOnGround(C);
	}
	else
	{
		S.Velocity = TFVec3();
	}
}

void WallRunMove(TFMoveCtx& C, float Dt)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	const TFMovementConVars& CV = C.CV;
	const TFVec3 N = S.WallNormal;

	S.Velocity -= N * S.Velocity.Dot(N);
	S.FallVelocity = 0.f;
	const float Fm = C.ForwardMove;
	const float Sm = C.SideMove;

	// In-surface axes. A ("forward") follows the facing along the wall; B = A x n is vertical-ish.
	// The exact basis construction is [I]; the side-axis switch at |dot(right, n)| >= 0.7 is [R].
	TFVec3 T = TFVec3(N.Y, -N.X, 0.f).Normalized();
	if (T.IsNearlyZero()) { T = C.YawRight; }
	TFVec3 A;
	if (std::fabs(C.ViewRight.Dot(N)) >= 0.7f)
	{
		A = (C.ViewFwd.Dot(T) >= 0.f) ? T : -T;
	}
	else
	{
		A = (C.ViewFwd - N * C.ViewFwd.Dot(N)).Normalized();
		if (A.IsNearlyZero()) { A = T; }
	}
	const TFVec3 B = A.Cross(N).Normalized();
	const float K = A.Dot(T);
	float Sm2 = Sm + std::max(Fm, 0.f) * K * CV.wallrun_upwardAutoPush; // forward input pushes up
	const float Fm2 = Fm + std::max(K * Sm, 0.f) * Fm;
	if ((B.Z < -0.01f && Sm2 > 0.f) || (B.Z > 0.01f && Sm2 < 0.f)) { Sm2 = 0.f; } // never steer down
	TFVec3 Wish = A * Fm2 + B * Sm2;
	Wish -= N * Wish.Dot(N);

	const float Hang = WallHangFactor(C);
	float Slip;
	if (!S.bWallHanging)
	{
		Slip = Clamp01((C.Now - S.WallRunStartTime - CV.wallrun_slipstarttime) / std::max(CV.wallrun_slipduration, 1e-4f));
		if (std::fabs(Fm2) < 0.1f && std::fabs(Sm2) < 0.1f && Slip <= CV.wallrun_noInputSlipFrac) { Slip = CV.wallrun_noInputSlipFrac; }
	}
	else
	{
		Slip = Clamp01((S.WallHangElapsed - CV.wallrun_hangslipstarttime) / std::max(CV.wallrun_hangslipduration, 1e-4f));
	}

	// Friction: speed*(1 - dt*k), exponential, no stop speed.
	float Ramp = 1.f;
	if (C.Now < S.DamageImpulseNoDecelEndTime)
	{
		Ramp = (C.Now <= S.DamageImpulseNoDecelEndTime - 0.5f) ? 0.f : (C.Now - (S.DamageImpulseNoDecelEndTime - 0.5f)) * 2.f;
	}
	float Fr = P.WallrunFrictionScale * P.WallrunFriction * S.SurfaceFriction * Ramp;
	if (P.bWallstickEnabled) { Fr = 10.f; }
	const float Spd = S.Velocity.Length();
	if (Spd > 0.f)
	{
		const float NewSpd = std::max(0.f, (Spd - Spd * Dt * Fr) * Hang);
		const float Vz0 = S.Velocity.Z;
		S.Velocity *= NewSpd / Spd;
		if (Slip != 0.f && Vz0 < 0.f) { S.Velocity.Z = LerpF(S.Velocity.Z, Vz0, Slip); } // slipping: friction does not hold you up
	}

	const float SpeedMul = SpeedScale(C) * MaxSpeedScale(C);
	if (std::fabs(Wish.Z) > 1e-4f)
	{
		const float Ws = P.bWallstickEnabled ? 0.f : std::min(std::fabs(Wish.Z), 1.f) * P.WallrunMaxSpeedVertical * SpeedMul * Hang;
		Accelerate(C, TFVec3(0.f, 0.f, Wish.Z > 0.f ? 1.f : -1.f), Ws, (1.f - Slip) * P.WallrunAccelerateVertical, Dt);
	}
	if (Wish.Length2DSq() > 1e-4f)
	{
		const TFVec3 D = Wish.Normalized2D();
		const float Cap = (D.Dot(C.YawFwd) <= P.WallrunBackwardCos) ? P.WallrunMaxSpeedHorizontalBackward : P.WallrunMaxSpeedHorizontal;
		const float Ws = P.bWallstickEnabled ? 0.f : std::min(Wish.Length2D(), 1.f) * Cap * SpeedMul * Hang;
		Accelerate(C, D, Ws, P.WallrunAccelerateHorizontal * P.AccelScale * SpeedMul, Dt);
	}

	TFMoveHits Hits;
	TFVec3 Pos = S.Origin;
	TFVec3 Vel = S.Velocity;
	SlideMove(C, Pos, Vel, Dt, &Hits);
	S.Origin = Pos;
	S.Velocity = Vel;
	for (int I = 0; I < Hits.Num; ++I)
	{
		TryStartWallRun(C, Hits.Hits[I].EndPos, Hits.Hits[I].Normal, Hits.Hits[I].VelBeforeClip);
		if (!S.bWallRunning) { break; }
	}
}

void ImpactCheck(TFMoveCtx& C, const TFVec3& OldVel)
{
	const float Lost = OldVel.Length2D() - C.S.Velocity.Length2D();
	float Strength = 0.f;
	if (Lost > C.P.ImpactSpeed) { Strength = 1.f; }
	else if (Lost > 0.5f * C.P.ImpactSpeed) { Strength = kImpactPartialStrength; }
	C.S.LastImpactStrength = Strength;
	if (Strength > 0.f) { Event(C, ETFMoveEvent::Impact, Strength); }
}

float AirSlowMoTimeScale(const TFMoveCtx& C)
{
	if (C.CV.airslowmo_ground_immediate_end && C.S.bOnGround) { return 1.f; }
	return LerpF(1.f, C.P.AirSlowMoSpeed, C.S.AirSlowMoBlend);
}

// FullWalkMove (ground_misc.md sec.1): gravity half-step, jump, wallrun think, grapple pull,
// air or surface move, categorize, gravity half-step.
void FullWalkMove(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const float Dt = C.Dt * AirSlowMoTimeScale(C);
	C.MoveDt = Dt;

	UpdateDodgeState(C);
	GravityHalfStep(C, Dt);
	CheckVelocity(C);
	CheckJumpButton(C, false);
	WallRunThink(C);
	const bool bGrappleFlag = GrapplePull(C, Dt);
	CheckVelocity(C);

	const TFVec3 OldVel = S.Velocity;
	if (!S.bOnGround && !S.bWallRunning) { AirMove(C, Dt); }
	else if (S.bWallRunning) { WallRunMove(C, Dt); }
	else { WalkMoveGround(C, Dt); }

	ImpactCheck(C, OldVel);
	CategorizePosition(C, bGrappleFlag);
	FinishGravity(C, Dt);
	if (S.bOnGround || S.bWallRunning) { S.Velocity -= S.UpDir * S.Velocity.Dot(S.UpDir); }
	CheckVelocity(C);
	if (!S.bOnGround && !S.bWallRunning) { S.FallVelocity = std::max(0.f, -S.Velocity.Z); }
}

// -------------------------------------------------------------------------------------------
// Sprint, power, hover / slow-mo, status effects
// -------------------------------------------------------------------------------------------
void StopSprint(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (!S.bSprinting) { return; }
	if (C.CV.sprint_powerdrain != 0.f)
	{
		S.SuitPower = std::max(0.f, S.SuitPower - 0.1f * C.CV.sprint_powerdrain);
		S.TimeAllSuitDevicesOff = C.Now + 0.5f;
	}
	S.bSprinting = false;
	S.SprintEndedTime = C.Now;
}

void UpdateSprint(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	if (P.bSprintToggle && C.In.bSprintPressed) { S.bSprintToggleLatch = !S.bSprintToggleLatch; }
	const bool bRequested = P.bSprintToggle ? S.bSprintToggleLatch : C.In.bSprintHeld;
	// [I] stop conditions (the sprint input code was not analysed, P4): no forward input, ADS,
	// crouch transitions, ziplining, mantling.
	const bool bCrouching = S.DuckState == ETFDuckState::Ducking || S.DuckState == ETFDuckState::Ducked;
	const bool bAllowed = P.bSprintEnabled && C.In.bAlive && C.ForwardMove > 0.1f && !C.In.bAdsHeld
		&& !bCrouching && S.Zipline.State == 0 && !S.Mantle.bActive;
	if (bRequested && bAllowed)
	{
		if (!S.bSprinting && S.SuitPower >= P.SprintMinPower)
		{
			S.bSprinting = true;
			S.SprintStartedTime = C.Now;
		}
	}
	else
	{
		if (P.bSprintToggle && C.ForwardMove <= 0.1f) { S.bSprintToggleLatch = false; }
		StopSprint(C);
	}
}

void UpdateSprintTilt(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const TFMovementConVars& CV = C.CV;
	float Target = 0.f;
	if (S.bOnGround && !S.bWallRunning && C.Dt > 0.f)
	{
		const float Run = StanceSpeed(C, CurrentStance(C));
		const float Spr = StanceSprintSpeed(C, 0);
		const float Spd = S.Velocity.Length();
		float Delta = C.In.ViewYaw - S.PrevMoveYaw;
		while (Delta > 180.f) { Delta -= 360.f; }
		while (Delta < -180.f) { Delta += 360.f; }
		if (Spd >= 1.05f * Run && Spr > Run)
		{
			const float Turn = Clamp((Delta / C.Dt) / CV.sprinttilt_turnrange, -1.f, 1.f);
			Target = Clamp01((Spd - Run) / (Spr - Run)) * Turn;
		}
	}
	S.PrevMoveYaw = C.In.ViewYaw;
	const float Err = Target - S.SprintTilt;
	const float Remaining = std::max(std::fabs(Err) - std::fabs(S.SprintTiltVel * C.Dt), 0.f);
	float VDes = (Err >= 0.f ? 1.f : -1.f) * std::sqrt(2.f * CV.sprinttilt_accel * Remaining);
	VDes = Clamp(VDes, -CV.sprinttilt_maxvel, CV.sprinttilt_maxvel);
	const float VOld = S.SprintTiltVel;
	S.SprintTiltVel += Clamp(VDes - S.SprintTiltVel, -CV.sprinttilt_accel * C.Dt, CV.sprinttilt_accel * C.Dt);
	const float Before = S.SprintTilt;
	S.SprintTilt += 0.5f * (VOld + S.SprintTiltVel) * C.Dt;
	if (((Before - Target) * (S.SprintTilt - Target) <= 0.f) && std::fabs(S.SprintTiltVel) < 0.1f)
	{
		S.SprintTilt = Target;
		S.SprintTiltVel = 0.f;
	}
	S.SprintTilt = Clamp(S.SprintTilt, -1.f, 1.f);
}

void UpdateSuitPower(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	// Grapple power.
	if (C.Now > S.LastGrapplePowerUse + P.GrapplePowerRegenDelay && S.GrapplePower < 100.f)
	{
		const float Rate = P.GrapplePowerRegenRate * (P.bPowerCellKit ? P.PowerCellRegenMultiplier : 1.f);
		S.GrapplePower = std::min(100.f, S.GrapplePower + Rate * C.Dt);
	}
	// Suit power: sprint is the only device; with sprint_powerdrain 0 it never drains.
	if (S.bSprinting && C.CV.sprint_powerdrain != 0.f)
	{
		S.SuitPower -= C.CV.sprint_powerdrain * C.Dt;
		if (S.SuitPower < 0.f) { S.SuitPower = 0.f; StopSprint(C); }
	}
	else if (S.SuitPower < 100.f && C.Now >= S.TimeAllSuitDevicesOff)
	{
		S.SuitPower = std::min(100.f, S.SuitPower + P.PowerRegenRate * C.Dt);
	}
}

void UpdateHoverAndSlowMo(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const TFPilotSettings& P = C.P;
	const TFMovementConVars& CV = C.CV;
	const bool bAirborne = !S.bOnGround && !S.bWallRunning;
	// ADS hover (pas_ads_hover kit). The glide meter drain is not documented [?]; the meter is not drained.
	const float AirTime = C.Now - std::max(S.TimeLastTouchedGround, S.TimeLastJumped);
	S.bHovering = P.bHoverEnabled && C.In.bAdsHeld && bAirborne && AirTime >= P.HoverSafety
		&& S.Zipline.State == 0 && !S.Mantle.bActive && S.GlideMeter > 0.f;
	if (S.bHovering && !CV.airslowmo_when_hovering)
	{
		const float Kh = std::exp(-0.693147f / std::max(P.HoverTightnessHorizontal, 1e-3f) * C.Dt);
		const float Kv = std::exp(-0.693147f / std::max(P.HoverTightnessVertical, 1e-3f) * C.Dt);
		S.Velocity.X *= Kh;
		S.Velocity.Y *= Kh;
		S.Velocity.Z *= Kv;
	}
	float Target;
	if (CV.airslowmo_enabled && P.AirSlowMoSpeed != 1.f) { Target = S.bOnGround ? 0.f : S.AdsFraction; }
	else { Target = (CV.airslowmo_when_hovering && S.bHovering && !S.bOnGround) ? 1.f : 0.f; }
	if (Target > S.AirSlowMoBlend) { S.AirSlowMoBlend = std::min(Target, S.AirSlowMoBlend + C.Dt / std::max(CV.airslowmo_enter_time, 1e-3f)); }
	else { S.AirSlowMoBlend = std::max(Target, S.AirSlowMoBlend - C.Dt / std::max(CV.airslowmo_leave_time, 1e-3f)); }
}

void UpdateStatusEffects(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (S.SpeedBoostEndTime > 0.f)
	{
		if (C.Now >= S.SpeedBoostEndTime)
		{
			S.SpeedBoostSeverity = 0.f;
			S.SpeedBoostEndTime = 0.f;
		}
		else if (S.SpeedBoostFadeTime > 0.f && C.Now > S.SpeedBoostEndTime - S.SpeedBoostFadeTime)
		{
			S.SpeedBoostSeverity = S.SpeedBoostBaseSeverity * (S.SpeedBoostEndTime - C.Now) / S.SpeedBoostFadeTime;
		}
		else
		{
			S.SpeedBoostSeverity = S.SpeedBoostBaseSeverity;
		}
	}
}

void UpdateAds(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	const float Target = (C.In.bAdsHeld && C.In.bAlive) ? 1.f : 0.f;
	S.AdsFraction = Approach(S.AdsFraction, Target, C.Dt / std::max(C.P.AdsBlendTime, 1e-3f));
	if (C.In.bAdsPressed) { S.bAdsLatch = true; }
	if (!C.In.bAdsHeld) { S.bAdsLatch = false; }
}

void UpdateViewRoll(TFMoveCtx& C)
{
	// Client-side roll [I]: wallrun_maxViewTilt / wallrun_viewTiltSpeed reused for a simple ease.
	TFPilotState& S = C.S;
	const float Target = S.bWallRunning ? -C.ViewRight.Dot(S.WallNormal) * C.P.WallrunViewTilt : 0.f;
	S.ViewRoll += (Target - S.ViewRoll) * std::min(1.f, C.P.WallrunViewTiltSpeed * C.Dt);
}

void StuckCheck(TFMoveCtx& C)
{
	TFPilotState& S = C.S;
	if (!S.bNeedStuckCheck) { return; }
	if (!Sweep(C, S.Origin, S.Origin).bStartSolid) { S.bNeedStuckCheck = false; return; }
	// [I] simplified CheckStuck: try small upward and sideways offsets.
	const TFVec3 Offsets[6] = { {0.f, 0.f, 1.f}, {0.f, 0.f, 4.f}, {0.f, 0.f, 9.f}, {2.f, 0.f, 0.f}, {0.f, 2.f, 0.f}, {0.f, 0.f, 18.f} };
	for (const TFVec3& O : Offsets)
	{
		if (!Sweep(C, S.Origin + O, S.Origin + O).bStartSolid)
		{
			S.Origin += O;
			S.bNeedStuckCheck = false;
			return;
		}
		if (!Sweep(C, S.Origin - O, S.Origin - O).bStartSolid)
		{
			S.Origin -= O;
			S.bNeedStuckCheck = false;
			return;
		}
	}
}

void BuildViewAndInput(TFMoveCtx& C)
{
	const float Pitch = C.In.ViewPitch * kDegToRad;
	const float Yaw = C.In.ViewYaw * kDegToRad;
	const float Sp = std::sin(Pitch), Cp = std::cos(Pitch);
	const float Sy = std::sin(Yaw), Cy = std::cos(Yaw);
	C.ViewFwd = TFVec3(Cp * Cy, Cp * Sy, -Sp);   // Source AngleVectors
	C.ViewRight = TFVec3(Sy, -Cy, 0.f);          // roll 0
	C.ViewUp = TFVec3(Sp * Cy, Sp * Sy, Cp);
	C.YawFwd = TFVec3(Cy, Sy, 0.f);
	C.YawRight = TFVec3(Sy, -Cy, 0.f);

	const bool bAlive = C.In.bAlive;
	C.ForwardMove = bAlive ? Clamp(C.In.ForwardMove, -1.f, 1.f) : 0.f;
	C.SideMove = bAlive ? Clamp(C.In.SideMove, -1.f, 1.f) : 0.f;
	TFVec3 W = C.YawFwd * C.ForwardMove + C.YawRight * C.SideMove;
	if (W.Length() > 1.f) { W = W.Normalized(); }
	C.WishDir = W;
	C.bJumpPressed = bAlive && C.In.bJumpPressed;

	// Key edges derived from the analogue axes [I]: a key is held beyond 0.5 and pressed on the edge.
	const float PF = C.S.PrevForwardMove, PS = C.S.PrevSideMove;
	C.bFwdHeld = C.ForwardMove >= 0.5f;
	C.bBackHeld = C.ForwardMove <= -0.5f;
	C.bRightHeld = C.SideMove >= 0.5f;
	C.bLeftHeld = C.SideMove <= -0.5f;
	C.bFwdPressed = C.bFwdHeld && PF < 0.5f;
	C.bBackPressed = C.bBackHeld && PF > -0.5f;
	C.bRightPressed = C.bRightHeld && PS < 0.5f;
	C.bLeftPressed = C.bLeftHeld && PS > -0.5f;
}

} // namespace TFMoveCoreImpl

// ---------------------------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------------------------
float TFGetHullHeight(const TFPilotState& S, const TFPilotSettings& Settings)
{
	return S.bCrouchedHull ? Settings.CrouchHullHeight : Settings.StandHullHeight;
}

float TFGetSpeedScale(const TFPilotState& S, const TFPilotInput& In, const TFPilotSettings& Settings)
{
	return In.WeaponSpeedScale * Settings.MoveSpeedScale * (1.f - S.MoveSlowSeverity)
		* (1.f + Settings.SpeedBoostFactor * S.SpeedBoostSeverity);
}

void TFStartStim(TFPilotState& State, const TFPilotSettings& Settings, float Now, float Duration)
{
	// sh_stim.gnut: speed_boost severity 0.4 for duration + 0.5 s [D]; 0.25 s fade read as fade-out [I].
	State.SpeedBoostBaseSeverity = Settings.StimSeverity;
	State.SpeedBoostSeverity = Settings.StimSeverity;
	State.SpeedBoostEndTime = Now + Duration + Settings.StimExtraTime;
	State.SpeedBoostFadeTime = Settings.StimFadeOut;
}

void TFResetPilot(TFPilotState& State, const TFVec3& FeetOrigin)
{
	State = TFPilotState();
	State.Origin = FeetOrigin;
}

void TFPilotMove(TFPilotState& State, const TFPilotInput& Input, const TFPilotSettings& Settings, ITFWorld& World, float Dt)
{
	static const TFMovementConVars DefaultConVars{};
	TFPilotMoveEx(State, Input, Settings, DefaultConVars, World, Dt);
}

void TFPilotMoveEx(TFPilotState& State, const TFPilotInput& Input, const TFPilotSettings& Settings,
	const TFMovementConVars& ConVars, ITFWorld& World, float Dt)
{
	using namespace TFMoveCoreImpl;
	if (Dt <= 0.f) { return; }
	TFMoveCtx C(State, Input, Settings, ConVars, World);
	C.Dt = Dt;
	C.MoveDt = Dt;
	C.Now = World.Now();
	TFPilotState& S = State;
	S.CurTime = C.Now;
	S.TickCount++;

	BuildViewAndInput(C);

	// ReduceTimers.
	S.Zipline.LastCooldown = std::max(0.f, S.Zipline.LastCooldown - Dt);
	UpdateStatusEffects(C);

	// 1. Stuck check (skipped while traversing or ziplining).
	if (!S.Mantle.bActive && S.Zipline.State == 0) { StuckCheck(C); }

	// Zipline lost its rope.
	if (S.Zipline.State != 0 && !World.GetZipline(S.Zipline.RopeId)) { ZiplineDetach(C); }

	// 2. CategorizePosition.
	CategorizePosition(C, GrapplePullingNow(C));

	// 3. Duck.
	Duck(C);

	// Grapple update (PlayerMove step 14).
	GrappleUpdate(C);

	// 4. Air slow-mo blend (+ ADS hover) and ADS fraction.
	UpdateAds(C);
	UpdateHoverAndSlowMo(C);

	// Sprint state, then 5. max speed and 6. sprint tilt.
	UpdateSprint(C);
	S.MaxSpeed = MaxSpeedScale(C) * BaseMaxSpeed(C);
	UpdateSprintTilt(C);

	// Zipline attach.
	ZiplineTryAttach(C);

	// Movetype dispatch.
	if (S.Zipline.State != 0)
	{
		ZiplineMove(C);
	}
	else if (S.Mantle.bActive)
	{
		TraversalMove(C);
	}
	else if (CheckAutoMantle(C))
	{
		TraversalMove(C);
	}
	else
	{
		FullWalkMove(C);
	}

	// Post-steps.
	GrappleLineWrap(C);
	UpdateSuitPower(C);
	UpdateViewRoll(C);

	const float EyeTarget = LerpF(Settings.StandEyeHeight, Settings.CrouchEyeHeight, DuckFraction(S));
	S.EyeHeight = S.Mantle.bActive ? (S.Mantle.bCrouched ? Settings.CrouchEyeHeight : Settings.StandEyeHeight) : EyeTarget;
	S.PrevForwardMove = C.ForwardMove;
	S.PrevSideMove = C.SideMove;
}
