//--------------------------------------------------------------------------------
#include "App.h"
#include "Log.h"

#include <sstream>

#include "EventManager.h"
#include "EvtFrameStart.h"
#include "EvtChar.h"
#include "EvtKeyUp.h"
#include "EvtKeyDown.h"

#include "ScriptManager.h"

#include "SwapChainConfigDX11.h"
#include "Texture2dConfigDX11.h"
#include "RasterizerStateConfigDX11.h"
#include "BufferConfigDX11.h"
#include "MaterialGeneratorDX11.h"

using namespace Glyph3;
//--------------------------------------------------------------------------------
App AppInstance; // Provides an instance of the application
//--------------------------------------------------------------------------------


//--------------------------------------------------------------------------------
App::App() : Application( 640, 480, true )
{
	m_bSaveScreenshot = false;
}
//--------------------------------------------------------------------------------
bool App::ConfigureEngineComponents()
{
	// The application currently supplies the 
	int width = this->DisplayWidth( ); 
	int height = this->DisplayHeight( ); 
	bool windowed = this->DisplayWindowed( );

	// Set the render window parameters and initialize the window
	m_pWindow = new Win32RenderWindow();
	m_pWindow->SetPosition( 25, 25 );
	m_pWindow->SetSize( width, height );
	m_pWindow->SetCaption( std::wstring( L"Direct3D 11 Window #1" ) );
	m_pWindow->Initialize();

	// Record the application handle.

	this->SetWindowID( m_pWindow->GetHandle() );

	
	// Create the renderer and initialize it for the desired device
	// type and feature level.

	m_pRenderer11 = new RendererDX11();

	if ( !m_pRenderer11->Initialize( D3D_DRIVER_TYPE_HARDWARE, D3D_FEATURE_LEVEL_11_0 ) )
	{
		Log::Get().Write( L"Could not create hardware device, trying to create the reference device..." );

		if ( !m_pRenderer11->Initialize( D3D_DRIVER_TYPE_REFERENCE, D3D_FEATURE_LEVEL_11_0 ) )
		{
			ShowWindow( m_pWindow->GetHandle(), SW_HIDE );
			MessageBox( m_pWindow->GetHandle(), L"Could not create a hardware or software Direct3D 11 device - the program will now abort!", L"Hieroglyph 3 Rendering", MB_ICONEXCLAMATION | MB_SYSTEMMODAL );
			RequestTermination();			
			return( false );
		}

		// If using the reference device, utilize a fixed time step for any animations.
		m_pTimer->SetFixedTimeStep( 1.0f / 10.0f );
	}


	// Create a swap chain for the window that we started out with.  This
	// demonstrates using a configuration object for fast and concise object
	// creation.

	SwapChainConfigDX11 Config;
	Config.SetWidth( m_pWindow->GetWidth() );
	Config.SetHeight( m_pWindow->GetHeight() );
	Config.SetOutputWindow( m_pWindow->GetHandle() );
	m_iSwapChain = m_pRenderer11->CreateSwapChain( &Config );
	m_pWindow->SetSwapChain( m_iSwapChain );

	// We'll keep a copy of the render target index to use in later examples.

	m_iRenderTarget = m_pRenderer11->GetSwapChainRenderTargetViewID( m_iSwapChain );

	// Next we create a depth buffer for use in the traditional rendering
	// pipeline.

	Texture2dConfigDX11 DepthConfig;
	DepthConfig.SetDepthBuffer( width, height );
	int DepthID = m_pRenderer11->CreateTexture2D( &DepthConfig, 0 );
	m_iDepthTarget = m_pRenderer11->CreateDepthStencilView( DepthID, 0 );
	
	// Bind the swap chain render target and the depth buffer for use in 
	// rendering.  

	m_pRenderer11->BindRenderTargets( m_iRenderTarget, m_iDepthTarget );

	// Create a view port to use on the scene.  This basically selects the 
	// entire floating point area of the render target.

	D3D11_VIEWPORT viewport;
	viewport.Width = static_cast< float >( width );
	viewport.Height = static_cast< float >( height );
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;

	int ViewPort = m_pRenderer11->CreateViewPort( viewport );
	m_pRenderer11->SetViewPort( ViewPort );
	
	return( true );
}
//--------------------------------------------------------------------------------
void App::ShutdownEngineComponents()
{
	if ( m_pRenderer11 )
	{
		m_pRenderer11->Shutdown();
		delete m_pRenderer11;
	}

	if ( m_pWindow )
	{
		m_pWindow->Shutdown();
		delete m_pWindow;
	}
}
//--------------------------------------------------------------------------------
void App::Initialize()
{
	// Basic event handling is supported with the EventManager class.  This is a 
	// singleton class that allows an EventListener to register which events it
	// wants to receive.

	EventManager* pEventManager = EventManager::Get( );

	// The application object wants to know about these three events, so it 
	// registers itself with the appropriate event IDs.

	pEventManager->AddEventListener( SYSTEM_KEYBOARD_KEYUP, this );
	pEventManager->AddEventListener( SYSTEM_KEYBOARD_KEYDOWN, this );
	pEventManager->AddEventListener( SYSTEM_KEYBOARD_CHAR, this );


	// Load and initialize the geometry to be rendered.

	m_pGeometry = GeometryLoaderDX11::loadMS3DFile2( std::wstring( L"../Data/Models/box.ms3d" ) );
	m_pGeometry->LoadToBuffers();
	m_pGeometry->SetPrimitiveType( D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST );


	// Create the parameters for use with this effect

	m_TessParams = Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
	m_pRenderer11->SetVectorParameter( std::wstring( L"EdgeFactors" ), &m_TessParams );


	// Create the material for use by the entities.

	m_pMaterial = MaterialGeneratorDX11::GenerateWireFrame( *m_pRenderer11 );


	// Create the camera, and the render view that will produce an image of the 
	// from the camera's point of view of the scene.

	m_pCamera = new Camera();
	m_pCamera->GetNode()->Position() = Vector3f( 0.0f, 0.0f, -15.0f );
	m_pRenderView = new ViewPerspective( *m_pRenderer11, 0 );
	m_pRenderView->SetBackColor( Vector4f( 0.6f, 0.6f, 0.6f, 0.6f ) );
	m_pCamera->SetCameraView( m_pRenderView );

	// Create the scene and add the entities to it.  Then add the camera to the
	// scene so that it will be updated via the scene interface instead of 
	// manually manipulating it.

	m_pScene = new Scene();
	for ( int i = 0; i < 10; i++ )
	{
		m_pEntity[i] = new Entity3D();
		m_pEntity[i]->SetGeometry( m_pGeometry );
		m_pEntity[i]->SetMaterial( m_pMaterial, false );
		m_pEntity[i]->Position() = Vector3f( i * 4, 4.0f * ( i % 2 ) - 2.0f, 0.0f );

		m_pScene->AddEntity( m_pEntity[i] );
	}

	m_pScene->AddCamera( m_pCamera );
}
//--------------------------------------------------------------------------------
void App::Update()
{
	// Update the timer to determine the elapsed time since last frame.  This can 
	// then used for animation during the frame.

	m_pTimer->Update();

	// Send an event to everyone that a new frame has started.  This will be used
	// in later examples for using the material system with render views.

	EventManager::Get()->ProcessEvent( new EvtFrameStart() );

	// Manipulate the scene here - simply rotate the root of the scene in this
	// example.

	Matrix3f rotation;
	rotation.RotationX( m_pTimer->Elapsed() );
	m_pScene->GetRoot()->Rotation() *= rotation;

	// Update the scene, and then render all cameras within the scene.

	m_pScene->Update( m_pTimer->Elapsed() );
	m_pScene->Render( *m_pRenderer11 );

	// Present the results of the rendering to the output window.

	m_pRenderer11->Present( m_pWindow->GetHandle(), m_pWindow->GetSwapChain() );

	// Save a screenshot if desired.  This is done by pressing the 's' key, which
	// demonstrates how an event is sent and handled by an event listener (which
	// in this case is the application object itself).

	if ( m_bSaveScreenshot  )
	{
		m_bSaveScreenshot = false;
		m_pRenderer11->SaveTextureScreenShot( 0, std::wstring( L"BasicScenes_" ), D3DX11_IFF_BMP );
	}
}
//--------------------------------------------------------------------------------
void App::Shutdown()
{
	SAFE_DELETE( m_pRenderView );

	for ( int i = 0; i < 10; i++ )
		SAFE_DELETE( m_pEntity[i] );

	SAFE_DELETE( m_pCamera );
	SAFE_DELETE( m_pScene );


	// Print the framerate out for the log before shutting down.

	std::wstringstream out;
	out << L"Max FPS: " << m_pTimer->MaxFramerate();
	Log::Get().Write( out.str() );

	Application::Shutdown();
}
//--------------------------------------------------------------------------------
bool App::HandleEvent( IEvent* pEvent )
{
	eEVENT e = pEvent->GetEventType();

	if ( e == SYSTEM_KEYBOARD_KEYDOWN )
	{
		EvtKeyDown* pKeyDown = (EvtKeyDown*)pEvent;

		unsigned int key = pKeyDown->GetCharacterCode();

		return( true );
	}
	else if ( e == SYSTEM_KEYBOARD_KEYUP )
	{
		EvtKeyUp* pKeyUp = (EvtKeyUp*)pEvent;

		unsigned int key = pKeyUp->GetCharacterCode();

		if ( key == VK_ESCAPE ) // 'Esc' Key - Exit the application
		{
			this->RequestTermination();
			return( true );
		}
		else if ( key == 0x53 ) // 'S' Key - Save a screen shot for the next frame
		{
			m_bSaveScreenshot = true;
			return( true );
		}
		else
		{
			return( false );
		}
	}

	
	return( false );
}
//--------------------------------------------------------------------------------
std::wstring App::GetName( )
{
	return( std::wstring( L"BasicApplication" ) );
}
//--------------------------------------------------------------------------------