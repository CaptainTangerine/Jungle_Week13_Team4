#pragma once
#include "Core/Types/CoreTypes.h"

struct FPostProcessSettings
{
	// DOF 전용 설정
	float Aperture = 0.5f;			// 0 = pass disabled
	float FocusDistance = 20.0f;	// focus plane distance
	float FocalLength = 15.0f;		// <= FocusDistance
	int	  DOFSample = 32;			// Number of blur taps
};