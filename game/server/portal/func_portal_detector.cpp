//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A volume in which no portal can be placed. Keeps a global list loaded in from the map
//			and provides an interface with which prop_portal can get this list and avoid successfully
//			creating portals wholly or partially inside the volume.
//
// $NoKeywords: $
//======================================================================================//

#include "cbase.h"
#include "func_portal_detector.h"
#include "prop_portal_shared.h"
#include "portal_shareddefs.h"
#include "portal_util_shared.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Spawnflags
#define SF_START_INACTIVE			0x01
#define PORTALDETECT_THINK_TIME		.1f


LINK_ENTITY_TO_CLASS( func_portal_detector, CFuncPortalDetector );

BEGIN_DATADESC( CFuncPortalDetector )

	DEFINE_FIELD( m_bActive, FIELD_BOOLEAN ),
	DEFINE_KEYFIELD( m_iLinkageGroupID, FIELD_INTEGER, "LinkageGroupID" ),
	DEFINE_FIELD( m_bTouchingPortal1, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bTouchingPortal2, FIELD_BOOLEAN ),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Toggle", InputToggle ),

	DEFINE_THINKFUNC( ThinkFunc ),

	DEFINE_OUTPUT( m_OnStartTouchPortal1, "OnStartTouchPortal1" ),
	DEFINE_OUTPUT( m_OnStartTouchPortal2, "OnStartTouchPortal2" ),
	DEFINE_OUTPUT( m_OnStartTouchLinkedPortal, "OnStartTouchLinkedPortal" ),
	DEFINE_OUTPUT( m_OnStartTouchBothLinkedPortals, "OnStartTouchBothLinkedPortals" ),
	DEFINE_OUTPUT( m_EndTouchPortal, "EndTouchPortal" ),

	DEFINE_FUNCTION( IsActive ),

END_DATADESC()


void CFuncPortalDetector::Spawn()
{
	BaseClass::Spawn();

	if ( m_spawnflags & SF_START_INACTIVE )
	{
		m_bActive = false;
	}
	else
	{
		m_bActive = true;
	}
	m_bTouchingPortal1 = false;
	m_bTouchingPortal2 = false;

	// Bind to our model, cause we need the extents for bounds checking
	SetModel( STRING( GetModelName() ) );
	SetRenderMode( kRenderNone );	// Don't draw
	SetSolid( SOLID_VPHYSICS );		// we may want slanted walls, so we'll use OBB
	AddSolidFlags( FSOLID_NOT_SOLID );

	RegisterThinkContext( "portaldetect" );
	SetContextThink( &CFuncPortalDetector::ThinkFunc, gpGlobals->curtime, "portaldetect" );
}

void CFuncPortalDetector::OnActivate( void )
{
	Vector vMin, vMax;
	CollisionProp()->WorldSpaceAABB( &vMin, &vMax );

	Vector vBoxCenter = ( vMin + vMax ) * 0.5f;
	Vector vBoxExtents = ( vMax - vMin ) * 0.5f;

	bool bTouchedPortal1 = false;
	bool bTouchedPortal2 = false;

	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if( iPortalCount != 0 )
	{
		CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();
		for( int i = 0; i != iPortalCount; ++i )
		{
			CProp_Portal *pTempPortal = pPortals[i];

			//require that it's active and/or linked?

			if( pTempPortal->GetLinkageGroup() == m_iLinkageGroupID && UTIL_IsBoxIntersectingPortal( vBoxCenter, vBoxExtents, pTempPortal ) )
			{
				if( pTempPortal->IsPortal2() )
				{
					m_OnStartTouchPortal2.FireOutput( pTempPortal, this );
					m_bTouchingPortal2 = true;

					if ( pTempPortal->IsActivedAndLinked() )
					{
						bTouchedPortal2 = true;
						m_OnStartTouchLinkedPortal.FireOutput( pTempPortal, this );
					}
				}
				else
				{
					m_OnStartTouchPortal1.FireOutput( pTempPortal, this );
					m_bTouchingPortal1 = true;

					if ( pTempPortal->IsActivedAndLinked() )
					{
						bTouchedPortal1 = true;
						m_OnStartTouchLinkedPortal.FireOutput( pTempPortal, this );
					}
				}
			}
			else if ( m_bTouchingPortal1 || m_bTouchingPortal2 )
			{
				m_bTouchingPortal1 = false;
				m_bTouchingPortal2 = false;
				m_EndTouchPortal.FireOutput( pTempPortal, this );
			}
		}
	}

	if ( bTouchedPortal1 && bTouchedPortal2 )
	{
		m_OnStartTouchBothLinkedPortals.FireOutput( this, this );
	}
}

void CFuncPortalDetector::InputDisable( inputdata_t &inputdata )
{
	m_bActive = false;
}

void CFuncPortalDetector::InputEnable( inputdata_t &inputdata )
{
	m_bActive = true;

	OnActivate();
}

void CFuncPortalDetector::InputToggle( inputdata_t &inputdata )
{
	m_bActive = !m_bActive;

	if ( m_bActive )
	{
		OnActivate();
	}
}

void CFuncPortalDetector::ThinkFunc( void )
{
	Vector vMin, vMax;
	CollisionProp()->WorldSpaceAABB( &vMin, &vMax );

	Vector vBoxCenter = ( vMin + vMax ) * 0.5f;
	Vector vBoxExtents = ( vMax - vMin ) * 0.5f;

	bool bCurrentlyTouchingActivePortal1 = false;
	bool bCurrentlyTouchingActivePortal2 = false;

	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if ( iPortalCount != 0 )
	{
		CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();
		for ( int i = 0; i != iPortalCount; ++i )
		{
			CProp_Portal *pTempPortal = pPortals[i];

			//require that it's active and/or linked?

			if ( pTempPortal->GetLinkageGroup() == m_iLinkageGroupID && UTIL_IsBoxIntersectingPortal( vBoxCenter, vBoxExtents, pTempPortal ) )
			{
				if ( pTempPortal->IsPortal2() )
				{
					m_OnStartTouchPortal2.FireOutput( pTempPortal, this );


					if ( pTempPortal->IsActivedAndLinked() )
					{
						bCurrentlyTouchingActivePortal2 = true;
						m_OnStartTouchLinkedPortal.FireOutput( pTempPortal, this );
					}
				}
				else
				{
					m_OnStartTouchPortal1.FireOutput( pTempPortal, this );

					if ( pTempPortal->IsActivedAndLinked() )
					{
						bCurrentlyTouchingActivePortal1 = true;
						m_OnStartTouchLinkedPortal.FireOutput( pTempPortal, this );
					}
				}
			}
			
		}

		if ( bCurrentlyTouchingActivePortal1 )
			m_bTouchingPortal1 = true;
		if ( bCurrentlyTouchingActivePortal2 )
			m_bTouchingPortal2 = true;

		if ( ( m_bTouchingPortal1 && !bCurrentlyTouchingActivePortal1 ) || ( m_bTouchingPortal2 && !bCurrentlyTouchingActivePortal2 ) )
		{
			m_bTouchingPortal1 = false;
			m_bTouchingPortal2 = false;
			m_EndTouchPortal.FireOutput( this, this );
		}
	}

	if ( bCurrentlyTouchingActivePortal1 && bCurrentlyTouchingActivePortal1 )
	{
		m_OnStartTouchBothLinkedPortals.FireOutput( this, this );
	}

	SetContextThink( &CFuncPortalDetector::ThinkFunc, gpGlobals->curtime + PORTALDETECT_THINK_TIME , "portaldetect" );
}
