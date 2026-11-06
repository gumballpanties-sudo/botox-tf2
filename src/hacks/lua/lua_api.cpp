// lua bindings, TF2 x64. An aim TU (CMake: /FI aim/prefix.h): the Amalgam SDK gives the verified
// interfaces and entity layout. dih-side things come through lua_bridge.h.
#include "../aim/sdk.h"
#include "../aim/aim.h"
#include "../../dependencies/imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "lua_api.h"
#include "lua_bridge.h"
#include "lua_draw.h"

void DebugLog( const char* fmt, ... );

using n_lua::element_t;
using n_lua::script_t;
using word_t = std::uintptr_t;

namespace
{
	constexpr const char* k_element_meta = "botox.element";
	constexpr const char* k_entity_meta  = "botox.entity";
	constexpr const char* k_cmd_meta     = "botox.cmd";
	constexpr const char* k_event_meta   = "botox.event";

	struct element_ref_t {
		int m_index;
	};

	struct entity_ref_t {
		int m_index;
	};

	/* ---- seh: plain functions, no c++ objects, so __try is legal under /EHsc ---- */

	bool seh_copy( void* destination, const void* source, const std::size_t size )
	{
		__try {
			std::memcpy( destination, source, size );
			return true;
		} __except ( EXCEPTION_EXECUTE_HANDLER ) {
			return false;
		}
	}

	int seh_strlen( const char* text, const int max )
	{
		__try {
			int length = 0;
			while ( length < max && text[ length ] )
				length++;
			return length;
		} __except ( EXCEPTION_EXECUTE_HANDLER ) {
			return -1;
		}
	}

	bool seh_write( void* destination, const void* source, const std::size_t size )
	{
		if ( seh_copy( destination, source, size ) )
			return true;

		DWORD old_protect = 0;
		if ( !VirtualProtect( destination, size, PAGE_EXECUTE_READWRITE, &old_protect ) )
			return false;

		const bool ok = seh_copy( destination, source, size );
		VirtualProtect( destination, size, old_protect, &old_protect );
		FlushInstructionCache( GetCurrentProcess( ), destination, size );
		return ok;
	}

	/* x64: one calling convention (rcx rdx r8 r9 + stack), the names only pick how many leading args are "this" */
	template< std::size_t >
	using word_n = word_t;

	template< std::size_t... I >
	word_t call_n( word_t fn, const word_t* a, std::index_sequence< I... > )
	{
		return reinterpret_cast< word_t ( * )( word_n< I >... ) >( fn )( a[ I ]... );
	}

	using invoker_t = word_t ( * )( word_t, const word_t* );

	template< std::size_t count >
	word_t invoke( word_t fn, const word_t* a )
	{
		return call_n( fn, a, std::make_index_sequence< count >{ } );
	}

	constexpr std::size_t k_max_call_args = 14;

	template< std::size_t... N >
	constexpr std::array< invoker_t, sizeof...( N ) > invoker_row( std::index_sequence< N... > )
	{
		return { &invoke< N >... };
	}

	/* [ total args ] */
	const std::array< invoker_t, k_max_call_args + 1 > k_invokers = invoker_row( std::make_index_sequence< k_max_call_args + 1 >{ } );

	bool seh_invoke( invoker_t invoker, word_t fn, const word_t* args, word_t* out )
	{
		__try {
			*out = invoker( fn, args );
			return true;
		} __except ( EXCEPTION_EXECUTE_HANDLER ) {
			return false;
		}
	}

	/* ---- helpers ---- */

	word_t check_address( lua_State* L, const int index )
	{
		return static_cast< word_t >( luaL_checkinteger( L, index ) );
	}

	void push_address( lua_State* L, const void* pointer )
	{
		if ( pointer )
			lua_pushinteger( L, static_cast< lua_Integer >( reinterpret_cast< word_t >( pointer ) ) );
		else
			lua_pushnil( L );
	}

	float check_float( lua_State* L, const int index )
	{
		return static_cast< float >( luaL_checknumber( L, index ) );
	}

	float opt_float( lua_State* L, const int index, const float fallback )
	{
		return static_cast< float >( luaL_optnumber( L, index, fallback ) );
	}

	unsigned int opt_color( lua_State* L, const int index )
	{
		return static_cast< unsigned int >( luaL_optinteger( L, index, 0xFFFFFFFF ) );
	}

	void push_vector( lua_State* L, const Vec3& vector )
	{
		lua_pushnumber( L, vector.x );
		lua_pushnumber( L, vector.y );
		lua_pushnumber( L, vector.z );
	}

	void set_field( lua_State* L, const char* key, const lua_Number value )
	{
		lua_pushnumber( L, value );
		lua_setfield( L, -2, key );
	}

	void set_field( lua_State* L, const char* key, const lua_Integer value )
	{
		lua_pushinteger( L, value );
		lua_setfield( L, -2, key );
	}

	void set_field( lua_State* L, const char* key, const char* value )
	{
		lua_pushstring( L, value );
		lua_setfield( L, -2, key );
	}

	void set_bool( lua_State* L, const char* key, const bool value )
	{
		lua_pushboolean( L, value );
		lua_setfield( L, -2, key );
	}

	void add_table( lua_State* L, const char* name, const luaL_Reg* functions )
	{
		lua_newtable( L );
		luaL_setfuncs( L, functions, 0 );
		lua_setglobal( L, name );
	}

	/* I:: interfaces are the aim half's; until Aim_Init succeeded they are null */
	void require_sdk( lua_State* L )
	{
		if ( !LuaB_AimReady( ) || !I::EngineClient || !I::ClientEntityList || !I::GlobalVars )
			luaL_error( L, "botox sdk not loaded yet (aim half failed to init, see debug.log)" );
	}

	bool sdk_ready( )
	{
		return LuaB_AimReady( ) && I::EngineClient && I::ClientEntityList && I::GlobalVars;
	}

	CBaseEntity* entity_at( const int index )
	{
		if ( !sdk_ready( ) || index < 0 )
			return nullptr;
		IClientEntity* entity = I::ClientEntityList->GetClientEntity( index );
		return entity ? entity->As< CBaseEntity >( ) : nullptr;
	}

	CBaseEntity* entity_from_handle( const CBaseHandle& handle )
	{
		if ( !sdk_ready( ) )
			return nullptr;
		IClientEntity* entity = I::ClientEntityList->GetClientEntityFromHandle( handle );
		return entity ? entity->As< CBaseEntity >( ) : nullptr;
	}

	CBaseEntity* local_player( )
	{
		if ( !sdk_ready( ) || !I::EngineClient->IsInGame( ) )
			return nullptr;
		return entity_at( I::EngineClient->GetLocalPlayer( ) );
	}

	/* ---- callbacks ---- */

	int l_callbacks_register( lua_State* L )
	{
		const char* name = luaL_checkstring( L, 1 );
		luaL_checktype( L, 2, LUA_TFUNCTION );

		lua_getfield( L, LUA_REGISTRYINDEX, "botox.callbacks" );
		if ( lua_getfield( L, -1, name ) != LUA_TTABLE ) {
			lua_pop( L, 1 );
			lua_newtable( L );
			lua_pushvalue( L, -1 );
			lua_setfield( L, -3, name );
		}

		const lua_Integer size = static_cast< lua_Integer >( lua_rawlen( L, -1 ) );
		lua_pushvalue( L, 2 );
		lua_rawseti( L, -2, size + 1 );
		return 0;
	}

	int l_callbacks_unregister( lua_State* L )
	{
		const char* name = luaL_checkstring( L, 1 );
		luaL_checktype( L, 2, LUA_TFUNCTION );

		lua_getfield( L, LUA_REGISTRYINDEX, "botox.callbacks" );
		if ( lua_getfield( L, -1, name ) != LUA_TTABLE )
			return 0;

		const int list         = lua_gettop( L );
		const lua_Integer size = static_cast< lua_Integer >( lua_rawlen( L, list ) );

		for ( lua_Integer i = 1; i <= size; i++ ) {
			lua_rawgeti( L, list, i );
			const bool same = lua_rawequal( L, -1, 2 );
			lua_pop( L, 1 );
			if ( !same )
				continue;

			for ( lua_Integer j = i; j < size; j++ ) {
				lua_rawgeti( L, list, j + 1 );
				lua_rawseti( L, list, j );
			}
			lua_pushnil( L );
			lua_rawseti( L, list, size );
			break;
		}

		return 0;
	}

	/* ---- ui ---- */

	int create_element( lua_State* L, element_t&& element )
	{
		script_t* script = n_lua_api::script_of( L );
		int index        = 0;

		{
			std::lock_guard< std::mutex > lock( g_lua.m_mutex );
			g_lua.apply_saved( *script, element );
			script->m_elements.push_back( std::move( element ) );
			index = static_cast< int >( script->m_elements.size( ) ) - 1;
		}

		auto* ref    = static_cast< element_ref_t* >( lua_newuserdatauv( L, sizeof( element_ref_t ), 0 ) );
		ref->m_index = index;
		luaL_setmetatable( L, k_element_meta );
		return 1;
	}

	element_t make_element( lua_State* L, const n_lua::e_element_type type )
	{
		element_t element{ };
		element.m_type  = type;
		element.m_label = luaL_checkstring( L, 1 );
		return element;
	}

	int l_ui_checkbox( lua_State* L )
	{
		element_t element = make_element( L, n_lua::element_checkbox );
		element.m_bool    = lua_toboolean( L, 2 );
		return create_element( L, std::move( element ) );
	}

	int l_ui_slider_int( lua_State* L )
	{
		element_t element  = make_element( L, n_lua::element_slider_int );
		element.m_int_min  = static_cast< int >( luaL_checkinteger( L, 2 ) );
		element.m_int_max  = static_cast< int >( luaL_checkinteger( L, 3 ) );
		element.m_int      = std::clamp( static_cast< int >( luaL_optinteger( L, 4, element.m_int_min ) ), element.m_int_min, element.m_int_max );
		element.m_format   = luaL_optstring( L, 5, "%d" );
		return create_element( L, std::move( element ) );
	}

	int l_ui_slider_float( lua_State* L )
	{
		element_t element   = make_element( L, n_lua::element_slider_float );
		element.m_float_min = check_float( L, 2 );
		element.m_float_max = check_float( L, 3 );
		element.m_float     = std::clamp( opt_float( L, 4, element.m_float_min ), element.m_float_min, element.m_float_max );
		element.m_format    = luaL_optstring( L, 5, "%.2f" );
		return create_element( L, std::move( element ) );
	}

	int l_ui_combo( lua_State* L )
	{
		element_t element = make_element( L, n_lua::element_combo );
		luaL_checktype( L, 2, LUA_TTABLE );

		const lua_Integer size = static_cast< lua_Integer >( lua_rawlen( L, 2 ) );
		for ( lua_Integer i = 1; i <= size; i++ ) {
			lua_rawgeti( L, 2, i );
			element.m_items.emplace_back( luaL_checkstring( L, -1 ) );
			lua_pop( L, 1 );
		}

		if ( element.m_items.empty( ) )
			return luaL_error( L, "ui.combo: items table is empty" );

		element.m_int = std::clamp( static_cast< int >( luaL_optinteger( L, 3, 1 ) ) - 1, 0, static_cast< int >( element.m_items.size( ) ) - 1 );
		return create_element( L, std::move( element ) );
	}

	int l_ui_color( lua_State* L )
	{
		element_t element = make_element( L, n_lua::element_color );
		for ( int i = 0; i < 4; i++ )
			element.m_color[ i ] = std::clamp( static_cast< float >( luaL_optinteger( L, 2 + i, 255 ) ) / 255.f, 0.f, 1.f );
		return create_element( L, std::move( element ) );
	}

	int l_ui_keybind( lua_State* L )
	{
		element_t element = make_element( L, n_lua::element_keybind );
		element.m_bind    = key_bind_t{ std::clamp( static_cast< int >( luaL_optinteger( L, 2, 0 ) ), 0, 255 ),
                                     std::clamp( static_cast< int >( luaL_optinteger( L, 3, 1 ) ), 0, 2 ) };
		return create_element( L, std::move( element ) );
	}

	int l_ui_button( lua_State* L )
	{
		element_t element = make_element( L, n_lua::element_button );
		if ( !lua_isnoneornil( L, 2 ) ) {
			luaL_checktype( L, 2, LUA_TFUNCTION );
			lua_pushvalue( L, 2 );
			element.m_callback = luaL_ref( L, LUA_REGISTRYINDEX );
		}
		return create_element( L, std::move( element ) );
	}

	int l_ui_text( lua_State* L )
	{
		return create_element( L, make_element( L, n_lua::element_text ) );
	}

	int l_ui_input( lua_State* L )
	{
		element_t element = make_element( L, n_lua::element_input );
		strncpy_s( element.m_text, luaL_optstring( L, 2, "" ), _TRUNCATE );
		return create_element( L, std::move( element ) );
	}

	int element_index( lua_State* L )
	{
		return static_cast< const element_ref_t* >( luaL_checkudata( L, 1, k_element_meta ) )->m_index;
	}

	/* never call lua under g_lua.m_mutex: any push / error can run a __gc finalizer that re-enters ui ( std::mutex is not recursive ) */
	bool copy_element( lua_State* L, const int index, element_t& out, bool* active = nullptr )
	{
		std::lock_guard< std::mutex > lock( g_lua.m_mutex );
		auto& elements = n_lua_api::script_of( L )->m_elements;
		if ( index < 0 || index >= static_cast< int >( elements.size( ) ) )
			return false;

		element_t& element = elements[ index ];
		if ( active && element.m_type == n_lua::element_keybind )
			*active = LuaB_BindActive( element.m_bind.m_key, element.m_bind.m_key_style );

		out = element;
		return true;
	}

	int l_element_get( lua_State* L )
	{
		element_t element{ };
		bool active = false;
		if ( !copy_element( L, element_index( L ), element, &active ) )
			return luaL_error( L, "ui element is gone" );

		switch ( element.m_type ) {
		case n_lua::element_checkbox: lua_pushboolean( L, element.m_bool ); return 1;
		case n_lua::element_slider_int: lua_pushinteger( L, element.m_int ); return 1;
		case n_lua::element_slider_float: lua_pushnumber( L, element.m_float ); return 1;
		case n_lua::element_combo:
			lua_pushinteger( L, element.m_int + 1 );
			lua_pushstring( L, element.m_items[ element.m_int ].c_str( ) );
			return 2;
		case n_lua::element_color:
			for ( const float channel : element.m_color )
				lua_pushinteger( L, static_cast< lua_Integer >( channel * 255.f + 0.5f ) );
			return 4;
		case n_lua::element_keybind:
			lua_pushboolean( L, active );
			lua_pushinteger( L, element.m_bind.m_key );
			lua_pushinteger( L, element.m_bind.m_key_style );
			return 3;
		case n_lua::element_input: lua_pushstring( L, element.m_text ); return 1;
		case n_lua::element_text: lua_pushstring( L, element.m_label.c_str( ) ); return 1;
		default: lua_pushnil( L ); return 1;
		}
	}

	int l_element_set( lua_State* L )
	{
		const int index = element_index( L );
		element_t element{ };
		if ( !copy_element( L, index, element ) )
			return luaL_error( L, "ui element is gone" );

		switch ( element.m_type ) {
		case n_lua::element_checkbox: element.m_bool = lua_toboolean( L, 2 ); break;
		case n_lua::element_slider_int:
			element.m_int = std::clamp( static_cast< int >( luaL_checkinteger( L, 2 ) ), element.m_int_min, element.m_int_max );
			break;
		case n_lua::element_slider_float: element.m_float = std::clamp( check_float( L, 2 ), element.m_float_min, element.m_float_max ); break;
		case n_lua::element_combo:
			element.m_int = std::clamp( static_cast< int >( luaL_checkinteger( L, 2 ) ) - 1, 0, static_cast< int >( element.m_items.size( ) ) - 1 );
			break;
		case n_lua::element_color:
			for ( int i = 0; i < 4; i++ )
				element.m_color[ i ] = std::clamp( static_cast< float >( luaL_optinteger( L, 2 + i, 255 ) ) / 255.f, 0.f, 1.f );
			break;
		case n_lua::element_keybind:
			element.m_bind.m_key       = std::clamp( static_cast< int >( luaL_checkinteger( L, 2 ) ), 0, 255 );
			element.m_bind.m_key_style = std::clamp( static_cast< int >( luaL_optinteger( L, 3, element.m_bind.m_key_style ) ), 0, 2 );
			break;
		case n_lua::element_input: strncpy_s( element.m_text, luaL_checkstring( L, 2 ), _TRUNCATE ); break;
		case n_lua::element_text: element.m_label = luaL_checkstring( L, 2 ); break;
		default: break;
		}

		script_t* script = n_lua_api::script_of( L );
		std::lock_guard< std::mutex > lock( g_lua.m_mutex );
		if ( index >= static_cast< int >( script->m_elements.size( ) ) )
			return 0;

		element_t& live = script->m_elements[ index ];
		live.m_bool     = element.m_bool;
		live.m_int      = element.m_int;
		live.m_float    = element.m_float;
		live.m_bind     = element.m_bind;
		std::memcpy( live.m_color, element.m_color, sizeof( live.m_color ) );
		std::memcpy( live.m_text, element.m_text, sizeof( live.m_text ) );
		if ( live.m_type == n_lua::element_text )
			live.m_label = std::move( element.m_label );

		script->m_dirty = true;
		return 0;
	}

	int l_element_visible( lua_State* L )
	{
		const int index   = element_index( L );
		const bool change = lua_gettop( L ) >= 2;
		const bool value  = change && lua_toboolean( L, 2 );

		bool found = false, shown = false;
		{
			std::lock_guard< std::mutex > lock( g_lua.m_mutex );
			auto& elements = n_lua_api::script_of( L )->m_elements;
			if ( index >= 0 && index < static_cast< int >( elements.size( ) ) ) {
				if ( change )
					elements[ index ].m_visible = value;
				shown = elements[ index ].m_visible;
				found = true;
			}
		}

		if ( !found )
			return luaL_error( L, "ui element is gone" );

		lua_pushboolean( L, shown );
		return 1;
	}

	int l_element_label( lua_State* L )
	{
		element_t element{ };
		if ( !copy_element( L, element_index( L ), element ) )
			return luaL_error( L, "ui element is gone" );

		lua_pushstring( L, element.m_label.c_str( ) );
		return 1;
	}

	/* ---- render: paint callback only, recorded into lua_draw.h, replayed by render.cpp ---- */

	void require_paint( lua_State* L )
	{
		if ( !n_lua_api::s_in_paint )
			luaL_error( L, "render functions only work inside the paint callback" );
	}

	int opt_font( lua_State* L, const int index )
	{
		const int id = static_cast< int >( luaL_optinteger( L, index, LUAF_VERDANA ) );
		return ( id >= 0 && id <= 5 ) || ( id >= 100 && id <= 102 ) ? id : LUAF_VERDANA;
	}

	int l_color( lua_State* L )
	{
		const auto channel = [ L ]( const int index, const lua_Integer fallback ) {
			return static_cast< unsigned int >( std::clamp< lua_Integer >( luaL_optinteger( L, index, fallback ), 0, 255 ) );
		};

		lua_pushinteger( L, static_cast< lua_Integer >( IM_COL32( channel( 1, 255 ), channel( 2, 255 ), channel( 3, 255 ), channel( 4, 255 ) ) ) );
		return 1;
	}

	void screen_size( float& w, float& h )
	{
		int sw = 0, sh = 0;
		if ( sdk_ready( ) )
			I::EngineClient->GetScreenSize( sw, sh );
		w = static_cast< float >( sw );
		h = static_cast< float >( sh );
	}

	int l_render_screen_size( lua_State* L )
	{
		float w = 0.f, h = 0.f;
		screen_size( w, h );
		lua_pushnumber( L, w );
		lua_pushnumber( L, h );
		return 2;
	}

	int l_render_text( lua_State* L )
	{
		require_paint( L );

		std::size_t length       = 0;
		const char* text         = luaL_checklstring( L, 3, &length );
		const unsigned int color = opt_color( L, 4 );

		LuaDrawCmd& c = LuaDraw::Push( LUAD_TEXT );
		c.x0          = check_float( L, 1 );
		c.y0          = check_float( L, 2 );
		c.col         = color;
		c.col2        = IM_COL32( 0, 0, 0, ( color >> IM_COL32_A_SHIFT ) & 0xFF );
		c.font        = opt_font( L, 5 );
		c.flags       = static_cast< uint8_t >( luaL_optinteger( L, 6, 1 ) );
		c.size        = opt_float( L, 7, 0.f );
		LuaDraw::Text( text, length, c );
		return 0;
	}

	int l_render_text_size( lua_State* L )
	{
		require_paint( L );

		float w = 0.f, h = 0.f;
		LuaMeasure( opt_font( L, 2 ), luaL_checkstring( L, 1 ), opt_float( L, 3, 0.f ), w, h );
		lua_pushnumber( L, w );
		lua_pushnumber( L, h );
		return 2;
	}

	int l_render_line( lua_State* L )
	{
		require_paint( L );
		LuaDrawCmd& c = LuaDraw::Push( LUAD_LINE );
		c.x0          = check_float( L, 1 );
		c.y0          = check_float( L, 2 );
		c.x1          = check_float( L, 3 );
		c.y1          = check_float( L, 4 );
		c.col         = opt_color( L, 5 );
		c.thick       = opt_float( L, 6, 1.f );
		return 0;
	}

	int push_rect( lua_State* L, const bool filled )
	{
		require_paint( L );
		LuaDrawCmd& c = LuaDraw::Push( LUAD_RECT );
		c.x0          = check_float( L, 1 );
		c.y0          = check_float( L, 2 );
		c.x1          = check_float( L, 3 );
		c.y1          = check_float( L, 4 );
		c.col         = opt_color( L, 5 );
		c.flags       = filled ? 1 : 0;
		c.round       = opt_float( L, filled ? 6 : 7, 0.f );
		c.thick       = filled ? 1.f : opt_float( L, 6, 1.f );
		return 0;
	}

	int l_render_rect( lua_State* L )
	{
		return push_rect( L, false );
	}

	int l_render_rect_filled( lua_State* L )
	{
		return push_rect( L, true );
	}

	int l_render_gradient( lua_State* L )
	{
		require_paint( L );
		LuaDrawCmd& c = LuaDraw::Push( LUAD_GRAD );
		c.x0          = check_float( L, 1 );
		c.y0          = check_float( L, 2 );
		c.x1          = check_float( L, 3 );
		c.y1          = check_float( L, 4 );
		c.col         = opt_color( L, 5 );
		c.col2        = opt_color( L, 6 );
		c.round       = opt_float( L, 7, 0.f );
		return 0;
	}

	int l_render_circle( lua_State* L )
	{
		require_paint( L );
		LuaDrawCmd& c = LuaDraw::Push( LUAD_CIRCLE );
		c.x0          = check_float( L, 1 );
		c.y0          = check_float( L, 2 );
		c.x1          = check_float( L, 3 );
		c.col         = opt_color( L, 4 );
		c.pn          = static_cast< int >( std::clamp< lua_Integer >( luaL_optinteger( L, 5, 32 ), 3, 512 ) );
		c.thick       = opt_float( L, 6, 1.f );
		return 0;
	}

	int l_render_circle_filled( lua_State* L )
	{
		require_paint( L );
		LuaDrawCmd& c = LuaDraw::Push( LUAD_CIRCLE_FILLED );
		c.x0          = check_float( L, 1 );
		c.y0          = check_float( L, 2 );
		c.x1          = check_float( L, 3 );
		c.col         = opt_color( L, 4 );
		c.pn          = static_cast< int >( std::clamp< lua_Integer >( luaL_optinteger( L, 5, 32 ), 3, 512 ) );
		return 0;
	}

	void push_poly( const float* xy, const int count, const unsigned int fill, const unsigned int outline, const bool has_outline,
	                const float thickness )
	{
		LuaDrawCmd& c = LuaDraw::Push( LUAD_POLY );
		c.col         = fill;
		c.col2        = outline;
		c.flags       = has_outline ? 1 : 0;
		c.thick       = thickness;
		LuaDraw::Points( xy, std::min( count, 32 ), c );
	}

	int l_render_triangle( lua_State* L )
	{
		require_paint( L );

		const float points[ 6 ]  = { check_float( L, 1 ), check_float( L, 2 ), check_float( L, 3 ), check_float( L, 4 ), check_float( L, 5 ), check_float( L, 6 ) };
		const unsigned int color = opt_color( L, 7 );
		const bool filled        = lua_isnoneornil( L, 8 ) ? true : lua_toboolean( L, 8 );

		push_poly( points, 3, filled ? color : 0u, color, !filled, opt_float( L, 9, 1.f ) );
		return 0;
	}

	int l_render_poly( lua_State* L )
	{
		require_paint( L );
		luaL_checktype( L, 1, LUA_TTABLE );

		float points[ 64 ];
		const int count = std::min( static_cast< int >( lua_rawlen( L, 1 ) ) / 2, 32 );
		if ( count < 3 )
			return luaL_error( L, "render.poly needs at least 3 points ( x1, y1, x2, y2, x3, y3 )" );

		for ( int i = 0; i < count * 2; i++ ) {
			lua_rawgeti( L, 1, i + 1 );
			points[ i ] = static_cast< float >( luaL_checknumber( L, -1 ) );
			lua_pop( L, 1 );
		}

		const bool has_outline = !lua_isnoneornil( L, 3 );
		push_poly( points, count, opt_color( L, 2 ), has_outline ? opt_color( L, 3 ) : 0u, has_outline, opt_float( L, 4, 1.f ) );
		return 0;
	}

	int l_render_world_to_screen( lua_State* L )
	{
		require_paint( L );
		require_sdk( L );

		Vec3 screen{ };
		if ( !SDK::W2S( Vec3( check_float( L, 1 ), check_float( L, 2 ), check_float( L, 3 ) ), screen ) ) {
			lua_pushnil( L );
			return 1;
		}

		lua_pushnumber( L, screen.x );
		lua_pushnumber( L, screen.y );
		return 2;
	}

	/* ---- entities: userdata keeps the index, every call re-resolves it ---- */

	void push_entity( lua_State* L, CBaseEntity* entity )
	{
		if ( !entity ) {
			lua_pushnil( L );
			return;
		}

		auto* ref    = static_cast< entity_ref_t* >( lua_newuserdatauv( L, sizeof( entity_ref_t ), 0 ) );
		ref->m_index = entity->entindex( );
		luaL_setmetatable( L, k_entity_meta );
	}

	int entity_index( lua_State* L, const int index )
	{
		return static_cast< const entity_ref_t* >( luaL_checkudata( L, index, k_entity_meta ) )->m_index;
	}

	CBaseEntity* check_entity( lua_State* L, const int index = 1 )
	{
		const int entity_number = entity_index( L, index );
		require_sdk( L );
		CBaseEntity* entity = entity_at( entity_number );
		if ( !entity )
			luaL_error( L, "entity %d is gone", entity_number );
		return entity;
	}

	CBaseEntity* opt_entity( lua_State* L, const int index )
	{
		if ( lua_isnoneornil( L, index ) )
			return nullptr;

		return entity_at( entity_index( L, index ) );
	}

	int l_entities_local( lua_State* L )
	{
		push_entity( L, local_player( ) );
		return 1;
	}

	int l_entities_get( lua_State* L )
	{
		push_entity( L, entity_at( static_cast< int >( luaL_checkinteger( L, 1 ) ) ) );
		return 1;
	}

	int l_entities_from_handle( lua_State* L )
	{
		push_entity( L, entity_from_handle( CBaseHandle( static_cast< unsigned long >( luaL_checkinteger( L, 1 ) ) ) ) );
		return 1;
	}

	int l_entities_highest_index( lua_State* L )
	{
		require_sdk( L );
		lua_pushinteger( L, I::ClientEntityList->GetHighestEntityIndex( ) );
		return 1;
	}

	int l_entities_players( lua_State* L )
	{
		const bool include_dead    = lua_toboolean( L, 1 );
		const bool include_dormant = lua_toboolean( L, 2 );

		lua_newtable( L );
		lua_Integer count = 0;

		if ( !sdk_ready( ) || !I::EngineClient->IsInGame( ) )
			return 1;

		for ( int i = 1; i <= I::GlobalVars->maxClients; i++ ) {
			CBaseEntity* entity = entity_at( i );
			if ( !entity || !entity->IsPlayer( ) )
				continue;
			if ( !include_dead && !entity->As< CBasePlayer >( )->IsAlive( ) )
				continue;
			if ( !include_dormant && entity->IsDormant( ) )
				continue;

			push_entity( L, entity );
			lua_rawseti( L, -2, ++count );
		}

		return 1;
	}

	int l_entities_by_class( lua_State* L )
	{
		const char* wanted = luaL_checkstring( L, 1 );

		lua_newtable( L );
		lua_Integer count = 0;

		if ( !sdk_ready( ) )
			return 1;

		const int highest = I::ClientEntityList->GetHighestEntityIndex( );
		for ( int i = 0; i <= highest; i++ ) {
			CBaseEntity* entity = entity_at( i );
			if ( !entity )
				continue;

			ClientClass* client_class = entity->GetClientClass( );
			if ( !client_class || !client_class->m_pNetworkName || std::strcmp( client_class->m_pNetworkName, wanted ) != 0 )
				continue;

			push_entity( L, entity );
			lua_rawseti( L, -2, ++count );
		}

		return 1;
	}

	int l_entity_index( lua_State* L )
	{
		lua_pushinteger( L, entity_index( L, 1 ) );
		return 1;
	}

	int l_entity_ptr( lua_State* L )
	{
		push_address( L, check_entity( L ) );
		return 1;
	}

	int l_entity_is_valid( lua_State* L )
	{
		lua_pushboolean( L, entity_at( entity_index( L, 1 ) ) != nullptr );
		return 1;
	}

	int l_entity_is_player( lua_State* L )
	{
		lua_pushboolean( L, check_entity( L )->IsPlayer( ) );
		return 1;
	}

	int l_entity_is_alive( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		if ( entity->IsPlayer( ) )
			lua_pushboolean( L, entity->As< CBasePlayer >( )->IsAlive( ) );
		else if ( entity->IsBuilding( ) )
			lua_pushboolean( L, entity->As< CBaseObject >( )->m_iHealth( ) > 0 );
		else
			lua_pushboolean( L, true );
		return 1;
	}

	int l_entity_is_dormant( lua_State* L )
	{
		lua_pushboolean( L, check_entity( L )->IsDormant( ) );
		return 1;
	}

	int l_entity_is_enemy( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		CBaseEntity* local  = local_player( );
		lua_pushboolean( L, local && local != entity && local->m_iTeamNum( ) != entity->m_iTeamNum( ) );
		return 1;
	}

	int l_entity_team( lua_State* L )
	{
		lua_pushinteger( L, check_entity( L )->m_iTeamNum( ) );
		return 1;
	}

	int l_entity_health( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		if ( entity->IsPlayer( ) )
			lua_pushinteger( L, entity->As< CBasePlayer >( )->m_iHealth( ) );
		else if ( entity->IsBuilding( ) )
			lua_pushinteger( L, entity->As< CBaseObject >( )->m_iHealth( ) );
		else
			lua_pushinteger( L, 0 );
		return 1;
	}

	/* tf2 has no armor */
	int l_entity_armor( lua_State* L )
	{
		check_entity( L );
		lua_pushinteger( L, 0 );
		return 1;
	}

	int l_entity_flags( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		lua_pushinteger( L, entity->IsPlayer( ) ? entity->As< CBasePlayer >( )->m_fFlags( ) : 0 );
		return 1;
	}

	int l_entity_move_type( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		lua_pushinteger( L, entity->IsPlayer( ) ? entity->m_MoveType( ) : 0 );
		return 1;
	}

	int l_entity_origin( lua_State* L )
	{
		push_vector( L, check_entity( L )->GetAbsOrigin( ) );
		return 3;
	}

	int l_entity_velocity( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		push_vector( L, entity->IsPlayer( ) ? entity->As< CBasePlayer >( )->m_vecVelocity( ) : entity->GetAbsVelocity( ) );
		return 3;
	}

	int l_entity_eye_position( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		push_vector( L, entity->IsPlayer( ) ? entity->As< CBasePlayer >( )->GetEyePosition( ) : entity->GetAbsOrigin( ) );
		return 3;
	}

	int l_entity_mins_maxs( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		push_vector( L, entity->m_vecMins( ) );
		push_vector( L, entity->m_vecMaxs( ) );
		return 6;
	}

	int l_entity_class_name( lua_State* L )
	{
		ClientClass* client_class = check_entity( L )->GetClientClass( );
		lua_pushstring( L, client_class && client_class->m_pNetworkName ? client_class->m_pNetworkName : "" );
		return 1;
	}

	int l_entity_class_id( lua_State* L )
	{
		ClientClass* client_class = check_entity( L )->GetClientClass( );
		lua_pushinteger( L, client_class ? client_class->m_ClassID : -1 );
		return 1;
	}

	int l_entity_name( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );

		player_info_t info{ };
		if ( entity->IsPlayer( ) && I::EngineClient->GetPlayerInfo( entity->entindex( ), &info ) ) {
			info.name[ sizeof( info.name ) - 1 ] = '\0';
			lua_pushstring( L, info.name );
			return 1;
		}

		ClientClass* client_class = entity->GetClientClass( );
		lua_pushstring( L, client_class && client_class->m_pNetworkName ? client_class->m_pNetworkName : "" );
		return 1;
	}

	int l_entity_weapon( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		push_entity( L, entity->IsPlayer( ) ? entity_from_handle( entity->As< CBaseCombatCharacter >( )->m_hActiveWeapon( ) ) : nullptr );
		return 1;
	}

	/* GetNetVar walks every client class + table recursively: cached, game thread only (lua) */
	int cached_netvar( const char* table, const char* prop )
	{
		static std::unordered_map< std::string, int > s_cache;
		std::string key = std::string( table ) + "->" + prop;
		if ( const auto it = s_cache.find( key ); it != s_cache.end( ) )
			return it->second;
		const int offset = U::NetVars.GetNetVar( table, prop );
		s_cache.emplace( std::move( key ), offset );
		return offset;
	}

	/* this entity's own class has the prop: a typed read through a fixed-class offset on any other entity reads past it */
	int class_prop( CBaseEntity* entity, const char* prop )
	{
		ClientClass* client_class = entity->GetClientClass( );
		return client_class && client_class->m_pNetworkName ? cached_netvar( client_class->m_pNetworkName, prop ) : 0;
	}

	int l_entity_weapon_id( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		const int offset    = class_prop( entity, "m_iItemDefinitionIndex" );
		int value           = -1;
		if ( offset && !seh_copy( &value, reinterpret_cast< const void* >( reinterpret_cast< word_t >( entity ) + offset ), sizeof( value ) ) )
			value = -1;
		lua_pushinteger( L, value );
		return 1;
	}

	/* studio models only ( m_nHitboxSet = C_BaseAnimating ): brushes / sprites have no bones */
	bool setup_bones( CBaseEntity* entity, matrix3x4* bones )
	{
		return class_prop( entity, "m_nHitboxSet" ) && entity->SetupBones( bones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime );
	}

	int l_entity_bone_position( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		const int bone      = static_cast< int >( luaL_checkinteger( L, 2 ) );
		if ( bone < 0 || bone >= MAXSTUDIOBONES )
			return luaL_error( L, "bone %d out of range", bone );

		matrix3x4 bones[ MAXSTUDIOBONES ];
		if ( !setup_bones( entity, bones ) )
			return 0;

		push_vector( L, Vec3( bones[ bone ][ 0 ][ 3 ], bones[ bone ][ 1 ][ 3 ], bones[ bone ][ 2 ][ 3 ] ) );
		return 3;
	}

	int l_entity_hitbox_position( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		const int hitbox    = static_cast< int >( luaL_checkinteger( L, 2 ) );
		/* GetHitboxCenter only rejects too-large indices: a negative one walks off the hitbox set */
		if ( hitbox < 0 || hitbox >= MAXSTUDIOBONES )
			return luaL_error( L, "hitbox %d out of range", hitbox );

		matrix3x4 bones[ MAXSTUDIOBONES ];
		if ( !setup_bones( entity, bones ) )
			return 0;

		push_vector( L, entity->As< CBaseAnimating >( )->GetHitboxCenter( bones, hitbox ) );
		return 3;
	}

	enum e_value_type { value_int, value_float, value_bool, value_short, value_byte, value_vector, value_string, value_handle };

	e_value_type check_value_type( lua_State* L, const int index )
	{
		static const char* const names[ ] = { "int", "float", "bool", "short", "byte", "vector", "string", "handle", nullptr };
		return static_cast< e_value_type >( luaL_checkoption( L, index, "int", names ) );
	}

	int push_value( lua_State* L, const word_t address, const e_value_type type )
	{
		switch ( type ) {
		case value_float: {
			float value = 0.f;
			if ( !seh_copy( &value, reinterpret_cast< const void* >( address ), sizeof( value ) ) )
				return luaL_error( L, "bad read" );
			lua_pushnumber( L, value );
			return 1;
		}
		case value_bool: {
			unsigned char value = 0;
			if ( !seh_copy( &value, reinterpret_cast< const void* >( address ), sizeof( value ) ) )
				return luaL_error( L, "bad read" );
			lua_pushboolean( L, value != 0 );
			return 1;
		}
		case value_short: {
			short value = 0;
			if ( !seh_copy( &value, reinterpret_cast< const void* >( address ), sizeof( value ) ) )
				return luaL_error( L, "bad read" );
			lua_pushinteger( L, value );
			return 1;
		}
		case value_byte: {
			unsigned char value = 0;
			if ( !seh_copy( &value, reinterpret_cast< const void* >( address ), sizeof( value ) ) )
				return luaL_error( L, "bad read" );
			lua_pushinteger( L, value );
			return 1;
		}
		case value_vector: {
			float value[ 3 ] = { };
			if ( !seh_copy( value, reinterpret_cast< const void* >( address ), sizeof( value ) ) )
				return luaL_error( L, "bad read" );
			push_vector( L, Vec3( value[ 0 ], value[ 1 ], value[ 2 ] ) );
			return 3;
		}
		case value_string: {
			const int length = seh_strlen( reinterpret_cast< const char* >( address ), 4096 );
			if ( length < 0 )
				return luaL_error( L, "bad read" );
			lua_pushlstring( L, reinterpret_cast< const char* >( address ), static_cast< std::size_t >( length ) );
			return 1;
		}
		case value_handle: {
			unsigned int value = 0;
			if ( !seh_copy( &value, reinterpret_cast< const void* >( address ), sizeof( value ) ) )
				return luaL_error( L, "bad read" );
			push_entity( L, entity_from_handle( CBaseHandle( static_cast< unsigned long >( value ) ) ) );
			return 1;
		}
		default: {
			int value = 0;
			if ( !seh_copy( &value, reinterpret_cast< const void* >( address ), sizeof( value ) ) )
				return luaL_error( L, "bad read" );
			lua_pushinteger( L, value );
			return 1;
		}
		}
	}

	void write_value( lua_State* L, const word_t address, const e_value_type type, const int value_index )
	{
		void* const target = reinterpret_cast< void* >( address );
		bool ok            = false;

		switch ( type ) {
		case value_float: {
			const float value = check_float( L, value_index );
			ok                = seh_write( target, &value, sizeof( value ) );
			break;
		}
		case value_bool: {
			const unsigned char value = lua_toboolean( L, value_index ) ? 1 : 0;
			ok                        = seh_write( target, &value, sizeof( value ) );
			break;
		}
		case value_short: {
			const short value = static_cast< short >( luaL_checkinteger( L, value_index ) );
			ok                = seh_write( target, &value, sizeof( value ) );
			break;
		}
		case value_byte: {
			const unsigned char value = static_cast< unsigned char >( luaL_checkinteger( L, value_index ) );
			ok                        = seh_write( target, &value, sizeof( value ) );
			break;
		}
		case value_vector: {
			const float value[ 3 ] = { check_float( L, value_index ), check_float( L, value_index + 1 ), check_float( L, value_index + 2 ) };
			ok                     = seh_write( target, value, sizeof( value ) );
			break;
		}
		case value_string: luaL_error( L, "string props are read only" ); return;
		default: {
			const int value = static_cast< int >( luaL_checkinteger( L, value_index ) );
			ok              = seh_write( target, &value, sizeof( value ) );
			break;
		}
		}

		if ( !ok )
			luaL_error( L, "bad write" );
	}

	/* "CTFPlayer->m_iHealth" = U::NetVars.GetNetVar( "CTFPlayer", "m_iHealth" ), the aim SDK's NETVAR names */
	int netvar_offset( const char* name )
	{
		const char* split = std::strstr( name, "->" );
		if ( !split || !sdk_ready( ) )
			return 0;

		const std::string table( name, split );
		return cached_netvar( table.c_str( ), split + 2 );
	}

	int check_netvar( lua_State* L, const int index )
	{
		const char* name = luaL_checkstring( L, index );
		const int offset = netvar_offset( name );
		if ( !offset )
			luaL_error( L, "no netvar '%s' ( format: \"CTFPlayer->m_iHealth\" )", name );
		return offset;
	}

	int l_entity_get_prop( lua_State* L )
	{
		CBaseEntity* entity  = check_entity( L );
		const word_t address = reinterpret_cast< word_t >( entity ) + check_netvar( L, 2 ) + static_cast< word_t >( luaL_optinteger( L, 4, 0 ) );
		return push_value( L, address, check_value_type( L, 3 ) );
	}

	int l_entity_set_prop( lua_State* L )
	{
		CBaseEntity* entity  = check_entity( L );
		const word_t address = reinterpret_cast< word_t >( entity ) + check_netvar( L, 2 );
		write_value( L, address, check_value_type( L, 3 ), 4 );
		return 0;
	}

	/* prediction data map ( C_BaseEntity::GetPredDescMap, vtable 15 ). embedded maps add their own offset */
	int find_datamap( datamap_t* map, const char* name, const int depth )
	{
		for ( ; map && depth < 16; map = map->baseMap ) {
			for ( int i = 0; i < map->dataNumFields; i++ ) {
				const typedescription_t& field = map->dataDesc[ i ];
				if ( !field.fieldName )
					continue;
				if ( std::strcmp( field.fieldName, name ) == 0 )
					return field.fieldOffset[ TD_OFFSET_NORMAL ];
				if ( field.fieldType == FIELD_EMBEDDED && field.td )
					if ( const int inner = find_datamap( field.td, name, depth + 1 ) )
						return field.fieldOffset[ TD_OFFSET_NORMAL ] + inner;
			}
		}
		return 0;
	}

	int check_datafield( lua_State* L, CBaseEntity* entity, const int index )
	{
		const char* name = luaL_checkstring( L, index );
		const int offset = find_datamap( entity->GetPredDescMap( ), name, 0 );
		if ( !offset )
			luaL_error( L, "no datamap field '%s'", name );
		return offset;
	}

	int l_entity_get_datafield( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		return push_value( L, reinterpret_cast< word_t >( entity ) + check_datafield( L, entity, 2 ), check_value_type( L, 3 ) );
	}

	int l_entity_set_datafield( lua_State* L )
	{
		CBaseEntity* entity = check_entity( L );
		write_value( L, reinterpret_cast< word_t >( entity ) + check_datafield( L, entity, 2 ), check_value_type( L, 3 ), 4 );
		return 0;
	}

	int l_entity_eq( lua_State* L )
	{
		lua_pushboolean( L, entity_index( L, 1 ) == entity_index( L, 2 ) );
		return 1;
	}

	int l_entity_tostring( lua_State* L )
	{
		lua_pushfstring( L, "entity(%d)", entity_index( L, 1 ) );
		return 1;
	}

	/* ---- engine / globals ---- */

	int l_engine_exec( lua_State* L )
	{
		require_sdk( L );
		I::EngineClient->ClientCmd_Unrestricted( luaL_checkstring( L, 1 ) );
		return 0;
	}

	int l_engine_in_game( lua_State* L )
	{
		lua_pushboolean( L, sdk_ready( ) && I::EngineClient->IsInGame( ) );
		return 1;
	}

	int l_engine_connected( lua_State* L )
	{
		lua_pushboolean( L, sdk_ready( ) && I::EngineClient->IsConnected( ) );
		return 1;
	}

	int l_engine_local_index( lua_State* L )
	{
		lua_pushinteger( L, sdk_ready( ) ? I::EngineClient->GetLocalPlayer( ) : 0 );
		return 1;
	}

	int l_engine_max_clients( lua_State* L )
	{
		lua_pushinteger( L, sdk_ready( ) ? I::EngineClient->GetMaxClients( ) : 0 );
		return 1;
	}

	int l_engine_level_name( lua_State* L )
	{
		const char* name = sdk_ready( ) ? I::EngineClient->GetLevelName( ) : nullptr;
		lua_pushstring( L, name ? name : "" );
		return 1;
	}

	/* "maps/ctf_2fort.bsp" -> "ctf_2fort" ( tf2 has no GetLevelNameShort ) */
	int l_engine_map_name( lua_State* L )
	{
		const char* name = sdk_ready( ) ? I::EngineClient->GetLevelName( ) : nullptr;
		std::string_view view = name ? name : "";
		if ( const std::size_t slash = view.find_last_of( "/\\" ); slash != std::string_view::npos )
			view.remove_prefix( slash + 1 );
		if ( view.size( ) > 4 && view.substr( view.size( ) - 4 ) == ".bsp" )
			view.remove_suffix( 4 );
		lua_pushlstring( L, view.data( ), view.size( ) );
		return 1;
	}

	int l_engine_view_angles( lua_State* L )
	{
		require_sdk( L );
		QAngle angles{ };
		I::EngineClient->GetViewAngles( angles );
		lua_pushnumber( L, angles.x );
		lua_pushnumber( L, angles.y );
		lua_pushnumber( L, angles.z );
		return 3;
	}

	int l_engine_set_view_angles( lua_State* L )
	{
		require_sdk( L );
		float yaw = std::remainder( check_float( L, 2 ), 360.f );
		QAngle angles( std::clamp( check_float( L, 1 ), -89.f, 89.f ), yaw, std::clamp( opt_float( L, 3, 0.f ), -50.f, 50.f ) );
		I::EngineClient->SetViewAngles( angles );
		return 0;
	}

	int l_engine_player_info( lua_State* L )
	{
		require_sdk( L );
		player_info_t info{ };
		if ( !I::EngineClient->GetPlayerInfo( static_cast< int >( luaL_checkinteger( L, 1 ) ), &info ) ) {
			lua_pushnil( L );
			return 1;
		}

		info.name[ sizeof( info.name ) - 1 ] = '\0';
		info.guid[ sizeof( info.guid ) - 1 ] = '\0';

		lua_newtable( L );
		set_field( L, "name", info.name );
		set_field( L, "user_id", static_cast< lua_Integer >( info.userID ) );
		set_field( L, "steam_id", info.guid );
		set_field( L, "friends_id", static_cast< lua_Integer >( info.friendsID ) );
		set_field( L, "xuid", static_cast< lua_Integer >( info.friendsID ? 76561197960265728ll + info.friendsID : 0 ) );
		set_bool( L, "bot", info.fakeplayer );
		set_bool( L, "hltv", info.ishltv );
		return 1;
	}

	int l_engine_player_for_user_id( lua_State* L )
	{
		require_sdk( L );
		lua_pushinteger( L, I::EngineClient->GetPlayerForUserID( static_cast< int >( luaL_checkinteger( L, 1 ) ) ) );
		return 1;
	}

	int l_engine_playing_demo( lua_State* L )
	{
		lua_pushboolean( L, sdk_ready( ) && I::EngineClient->IsPlayingDemo( ) );
		return 1;
	}

	int l_engine_latency( lua_State* L )
	{
		INetChannelInfo* channel = sdk_ready( ) ? I::EngineClient->GetNetChannelInfo( ) : nullptr;
		lua_pushnumber( L, channel ? channel->GetLatency( 0 ) : 0.f );
		lua_pushnumber( L, channel ? channel->GetLatency( 1 ) : 0.f );
		return 2;
	}

	CGlobalVarsBase* check_globals( lua_State* L )
	{
		require_sdk( L );
		return I::GlobalVars;
	}

	int l_globals_realtime( lua_State* L )
	{
		lua_pushnumber( L, check_globals( L )->realtime );
		return 1;
	}

	int l_globals_curtime( lua_State* L )
	{
		lua_pushnumber( L, check_globals( L )->curtime );
		return 1;
	}

	int l_globals_frametime( lua_State* L )
	{
		lua_pushnumber( L, check_globals( L )->frametime );
		return 1;
	}

	int l_globals_absframetime( lua_State* L )
	{
		lua_pushnumber( L, check_globals( L )->absoluteframetime );
		return 1;
	}

	int l_globals_tickcount( lua_State* L )
	{
		lua_pushinteger( L, check_globals( L )->tickcount );
		return 1;
	}

	int l_globals_tickinterval( lua_State* L )
	{
		lua_pushnumber( L, check_globals( L )->interval_per_tick );
		return 1;
	}

	int l_globals_framecount( lua_State* L )
	{
		lua_pushinteger( L, check_globals( L )->framecount );
		return 1;
	}

	int l_globals_maxclients( lua_State* L )
	{
		lua_pushinteger( L, check_globals( L )->maxClients );
		return 1;
	}

	/* ---- cvar ---- */

	ConVar* find_cvar( lua_State* L )
	{
		const char* name = luaL_checkstring( L, 1 );
		return LuaB_AimReady( ) && I::CVar ? I::CVar->FindVar( name ) : nullptr;
	}

	int l_cvar_exists( lua_State* L )
	{
		lua_pushboolean( L, find_cvar( L ) != nullptr );
		return 1;
	}

	int l_cvar_get_int( lua_State* L )
	{
		ConVar* var = find_cvar( L );
		if ( !var )
			lua_pushnil( L );
		else
			lua_pushinteger( L, var->GetInt( ) );
		return 1;
	}

	int l_cvar_get_float( lua_State* L )
	{
		ConVar* var = find_cvar( L );
		if ( !var )
			lua_pushnil( L );
		else
			lua_pushnumber( L, var->GetFloat( ) );
		return 1;
	}

	int l_cvar_get_string( lua_State* L )
	{
		ConVar* var       = find_cvar( L );
		const char* value = var ? var->GetString( ) : nullptr;
		if ( !value )
			lua_pushnil( L );
		else
			lua_pushstring( L, value );
		return 1;
	}

	int l_cvar_set( lua_State* L )
	{
		ConVar* var = find_cvar( L );
		if ( !var )
			return luaL_error( L, "no cvar '%s'", lua_tostring( L, 1 ) );

		if ( lua_isinteger( L, 2 ) )
			var->SetValue( static_cast< int >( lua_tointeger( L, 2 ) ) );
		else if ( lua_type( L, 2 ) == LUA_TNUMBER )
			var->SetValue( static_cast< float >( lua_tonumber( L, 2 ) ) );
		else if ( lua_isboolean( L, 2 ) )
			var->SetValue( lua_toboolean( L, 2 ) ? 1 : 0 );
		else
			var->SetValue( luaL_checkstring( L, 2 ) );
		return 0;
	}

	/* ---- config: every botox setting by its cfg key ( config.cpp kEntries ) ---- */

	LuaCfgVar check_variable( lua_State* L )
	{
		const char* name = luaL_checkstring( L, 1 );
		LuaCfgVar var{ };
		if ( !LuaB_CfgFind( name, var ) )
			luaL_error( L, "config: no variable '%s'", name );
		return var;
	}

	const char* type_name( const int type )
	{
		switch ( type ) {
		case 0: return "bool";
		case 1: return "int";
		case 2: return "float";
		case 3: return "color";
		case 4: return "string";
		case 5: return "curve";
		default: return "unknown";
		}
	}

	int l_config_get( lua_State* L )
	{
		const LuaCfgVar var = check_variable( L );

		switch ( var.type ) {
		case 0: lua_pushboolean( L, *static_cast< bool* >( var.ptr ) ); return 1;
		case 1: lua_pushinteger( L, *static_cast< int* >( var.ptr ) ); return 1;
		case 2: lua_pushnumber( L, *static_cast< float* >( var.ptr ) ); return 1;
		case 3: {
			const float* color = static_cast< float* >( var.ptr );
			for ( int i = 0; i < 4; i++ )
				lua_pushinteger( L, static_cast< lua_Integer >( std::clamp( color[ i ], 0.f, 1.f ) * 255.f + 0.5f ) );
			return 4;
		}
		case 4: {
			const char* text = static_cast< const char* >( var.ptr );
			lua_pushlstring( L, text, strnlen( text, var.size ) );
			return 1;
		}
		default: lua_pushnil( L ); return 1;
		}
	}

	float color_channel( lua_State* L, const int index )
	{
		return static_cast< float >( std::clamp< lua_Integer >( luaL_optinteger( L, index, 255 ), 0, 255 ) ) / 255.f;
	}

	int l_config_set( lua_State* L )
	{
		const LuaCfgVar var = check_variable( L );

		switch ( var.type ) {
		case 0: *static_cast< bool* >( var.ptr ) = lua_toboolean( L, 2 ); break;
		case 1: *static_cast< int* >( var.ptr ) = static_cast< int >( luaL_checkinteger( L, 2 ) ); break;
		case 2: *static_cast< float* >( var.ptr ) = check_float( L, 2 ); break;
		case 3: {
			float* color = static_cast< float* >( var.ptr );
			const float value[ 4 ] = { color_channel( L, 2 ), color_channel( L, 3 ), color_channel( L, 4 ), color_channel( L, 5 ) };
			std::memcpy( color, value, sizeof( value ) );
			break;
		}
		case 4: {
			const char* text = luaL_checkstring( L, 2 );
			if ( var.size )
				strncpy_s( static_cast< char* >( var.ptr ), var.size, text, _TRUNCATE );
			break;
		}
		default: return luaL_error( L, "config: cannot set this type" );
		}

		return 0;
	}

	int l_config_type( lua_State* L )
	{
		lua_pushstring( L, type_name( check_variable( L ).type ) );
		return 1;
	}

	int l_config_reset( lua_State* L )
	{
		const char* name = luaL_checkstring( L, 1 );
		if ( !LuaB_CfgReset( name ) )
			return luaL_error( L, "config: no variable '%s'", name );
		return 0;
	}

	int l_config_list( lua_State* L )
	{
		const int count = LuaB_CfgCount( );
		lua_createtable( L, 0, count );
		for ( int i = 0; i < count; i++ ) {
			LuaCfgVar var{ };
			if ( !LuaB_CfgAt( i, var ) || !var.key )
				continue;
			lua_pushstring( L, type_name( var.type ) );
			lua_setfield( L, -2, var.key );
		}
		return 1;
	}

	/* ---- trace ---- */

	class lua_trace_filter_t : public ITraceFilter
	{
	public:
		bool ShouldHitEntity( IHandleEntity* entity, int ) override
		{
			return entity != m_skip;
		}

		TraceType_t GetTraceType( ) const override
		{
			return TRACE_EVERYTHING;
		}

		IHandleEntity* m_skip = nullptr;
	};

	Vec3 check_vector( lua_State* L, const int index )
	{
		return Vec3( check_float( L, index ), check_float( L, index + 1 ), check_float( L, index + 2 ) );
	}

	int run_trace( lua_State* L, const Vec3& start, const Vec3& end, const Vec3* mins, const Vec3* maxs, const int mask_index, const int skip_index )
	{
		require_sdk( L );
		if ( !I::EngineTrace )
			return luaL_error( L, "trace: engine trace not loaded" );

		const unsigned int mask = static_cast< unsigned int >( luaL_optinteger( L, mask_index, MASK_SHOT ) );

		lua_trace_filter_t filter;
		filter.m_skip = lua_isnoneornil( L, skip_index ) ? local_player( ) : opt_entity( L, skip_index );

		CGameTrace trace{ };
		if ( mins && maxs )
			SDK::TraceHull( start, end, *mins, *maxs, mask, &filter, &trace );
		else
			SDK::Trace( start, end, mask, &filter, &trace );

		lua_newtable( L );
		set_field( L, "fraction", static_cast< lua_Number >( trace.fraction ) );
		set_field( L, "x", static_cast< lua_Number >( trace.endpos.x ) );
		set_field( L, "y", static_cast< lua_Number >( trace.endpos.y ) );
		set_field( L, "z", static_cast< lua_Number >( trace.endpos.z ) );
		set_field( L, "normal_x", static_cast< lua_Number >( trace.plane.normal.x ) );
		set_field( L, "normal_y", static_cast< lua_Number >( trace.plane.normal.y ) );
		set_field( L, "normal_z", static_cast< lua_Number >( trace.plane.normal.z ) );
		set_field( L, "contents", static_cast< lua_Integer >( trace.contents ) );
		set_field( L, "hitgroup", static_cast< lua_Integer >( trace.hitgroup ) );
		set_field( L, "hitbox", static_cast< lua_Integer >( trace.hitbox ) );
		set_field( L, "surface", trace.surface.name ? trace.surface.name : "" );
		set_bool( L, "hit", trace.DidHit( ) );
		set_bool( L, "start_solid", trace.startsolid );
		set_bool( L, "all_solid", trace.allsolid );

		push_entity( L, trace.m_pEnt );
		lua_setfield( L, -2, "entity" );
		return 1;
	}

	int l_trace_line( lua_State* L )
	{
		return run_trace( L, check_vector( L, 1 ), check_vector( L, 4 ), nullptr, nullptr, 7, 8 );
	}

	int l_trace_hull( lua_State* L )
	{
		const Vec3 mins = check_vector( L, 7 ), maxs = check_vector( L, 10 );
		return run_trace( L, check_vector( L, 1 ), check_vector( L, 4 ), &mins, &maxs, 13, 14 );
	}

	int l_trace_point_contents( lua_State* L )
	{
		require_sdk( L );
		if ( !I::EngineTrace )
			return luaL_error( L, "trace: engine trace not loaded" );
		/* tf2 IEngineTrace::GetPointContents has no mask argument: masked here */
		const unsigned int mask = static_cast< unsigned int >( luaL_optinteger( L, 4, 0xFFFFFFFF ) );
		lua_pushinteger( L, static_cast< lua_Integer >( static_cast< unsigned int >( I::EngineTrace->GetPointContents( check_vector( L, 1 ) ) ) & mask ) );
		return 1;
	}

	/* ---- input ---- */

	int l_input_is_key_down( lua_State* L )
	{
		lua_pushboolean( L, LuaB_KeyDown( static_cast< int >( luaL_checkinteger( L, 1 ) ) ) );
		return 1;
	}

	int l_input_mouse_pos( lua_State* L )
	{
		int x = 0, y = 0;
		LuaB_MousePos( x, y );
		lua_pushinteger( L, x );
		lua_pushinteger( L, y );
		return 2;
	}

	int l_input_menu_open( lua_State* L )
	{
		lua_pushboolean( L, LuaB_MenuOpen( ) );
		return 1;
	}

	/* tf2: menu open, game window not focused, or console up */
	int l_input_keys_blocked( lua_State* L )
	{
		lua_pushboolean( L, LuaB_MenuOpen( ) || !LuaB_WindowActive( ) || ( sdk_ready( ) && I::EngineClient->Con_IsVisible( ) ) );
		return 1;
	}

	/* ---- mem: raw access, every read/write seh guarded ---- */

	int l_mem_module( lua_State* L )
	{
		push_address( L, GetModuleHandleA( luaL_checkstring( L, 1 ) ) );
		return 1;
	}

	int l_mem_find_pattern( lua_State* L )
	{
		const char* module_name = luaL_checkstring( L, 1 );
		const char* pattern     = luaL_checkstring( L, 2 );
		if ( !GetModuleHandleA( module_name ) ) {
			lua_pushnil( L );
			return 1;
		}

		push_address( L, reinterpret_cast< void* >( U::Memory.FindSignature( module_name, pattern ) ) );
		return 1;
	}

	int l_mem_interface( lua_State* L )
	{
		using create_interface_t = void* ( * )( const char*, int* );

		HMODULE base = GetModuleHandleA( luaL_checkstring( L, 1 ) );
		const auto create = base ? reinterpret_cast< create_interface_t >( GetProcAddress( base, "CreateInterface" ) ) : nullptr;
		push_address( L, create ? create( luaL_checkstring( L, 2 ), nullptr ) : nullptr );
		return 1;
	}

	int l_mem_export( lua_State* L )
	{
		HMODULE base = GetModuleHandleA( luaL_checkstring( L, 1 ) );
		push_address( L, base ? reinterpret_cast< void* >( GetProcAddress( base, luaL_checkstring( L, 2 ) ) ) : nullptr );
		return 1;
	}

	template< class T >
	int l_mem_read( lua_State* L )
	{
		const word_t address = check_address( L, 1 );

		T value{ };
		if ( !seh_copy( &value, reinterpret_cast< const void* >( address ), sizeof( T ) ) )
			return luaL_error( L, "mem: bad read at %p", reinterpret_cast< void* >( address ) );

		if constexpr ( std::is_floating_point_v< T > )
			lua_pushnumber( L, value );
		else
			lua_pushinteger( L, static_cast< lua_Integer >( value ) );
		return 1;
	}

	template< class T >
	int l_mem_write( lua_State* L )
	{
		const word_t address = check_address( L, 1 );

		T value{ };
		if constexpr ( std::is_floating_point_v< T > )
			value = static_cast< T >( luaL_checknumber( L, 2 ) );
		else
			value = static_cast< T >( luaL_checkinteger( L, 2 ) );

		if ( !seh_write( reinterpret_cast< void* >( address ), &value, sizeof( T ) ) )
			return luaL_error( L, "mem: bad write at %p", reinterpret_cast< void* >( address ) );
		return 0;
	}

	int l_mem_read_string( lua_State* L )
	{
		const word_t address = check_address( L, 1 );
		const int max        = static_cast< int >( std::clamp< lua_Integer >( luaL_optinteger( L, 2, 256 ), 0, 1 << 20 ) );
		const int length     = seh_strlen( reinterpret_cast< const char* >( address ), max );
		if ( length < 0 )
			return luaL_error( L, "mem: bad read at %p", reinterpret_cast< void* >( address ) );

		lua_pushlstring( L, reinterpret_cast< const char* >( address ), static_cast< std::size_t >( length ) );
		return 1;
	}

	int l_mem_write_string( lua_State* L )
	{
		const word_t address = check_address( L, 1 );
		std::size_t length   = 0;
		const char* text     = luaL_checklstring( L, 2, &length );

		if ( !seh_write( reinterpret_cast< void* >( address ), text, length + 1 ) )
			return luaL_error( L, "mem: bad write at %p", reinterpret_cast< void* >( address ) );
		return 0;
	}

	int l_mem_read_bytes( lua_State* L )
	{
		const word_t address   = check_address( L, 1 );
		const std::size_t size = static_cast< std::size_t >( std::clamp< lua_Integer >( luaL_checkinteger( L, 2 ), 0, 1 << 20 ) );

		std::string out( size, '\0' );
		if ( size && !seh_copy( out.data( ), reinterpret_cast< const void* >( address ), size ) )
			return luaL_error( L, "mem: bad read at %p", reinterpret_cast< void* >( address ) );

		lua_pushlstring( L, out.data( ), out.size( ) );
		return 1;
	}

	int l_mem_write_bytes( lua_State* L )
	{
		const word_t address = check_address( L, 1 );
		std::size_t length   = 0;
		const char* bytes    = luaL_checklstring( L, 2, &length );

		if ( length && !seh_write( reinterpret_cast< void* >( address ), bytes, length ) )
			return luaL_error( L, "mem: bad write at %p", reinterpret_cast< void* >( address ) );
		return 0;
	}

	int l_mem_vfunc( lua_State* L )
	{
		const word_t object     = check_address( L, 1 );
		const lua_Integer index = luaL_checkinteger( L, 2 );

		word_t table = 0, function = 0;
		if ( !seh_copy( &table, reinterpret_cast< const void* >( object ), sizeof( table ) ) ||
		     !seh_copy( &function, reinterpret_cast< const void* >( table + static_cast< word_t >( index ) * sizeof( word_t ) ), sizeof( function ) ) )
			return luaL_error( L, "mem: bad vtable at %p", reinterpret_cast< void* >( object ) );

		lua_pushinteger( L, static_cast< lua_Integer >( function ) );
		return 1;
	}

	/* mem.call( address, "thiscall", this, args... ): integer args, rax back. x64 = one convention, the name only
	   checks the leading register args are there */
	int l_mem_call( lua_State* L )
	{
		static const char* const conventions[ ] = { "cdecl", "stdcall", "thiscall", "fastcall", nullptr };

		const word_t function = check_address( L, 1 );
		const int convention  = luaL_checkoption( L, 2, "cdecl", conventions );
		const int args        = lua_gettop( L ) - 2;
		const int fixed       = convention == 2 ? 1 : convention == 3 ? 2 : 0;

		if ( args < fixed )
			return luaL_error( L, "mem.call: %s needs %d leading register arg(s)", conventions[ convention ], fixed );
		if ( args > static_cast< int >( k_max_call_args ) )
			return luaL_error( L, "mem.call: at most %d args", static_cast< int >( k_max_call_args ) );

		word_t values[ k_max_call_args ] = { };
		for ( int i = 0; i < args; i++ ) {
			const int index = i + 3;
			if ( lua_isboolean( L, index ) )
				values[ i ] = lua_toboolean( L, index ) ? 1 : 0;
			else if ( lua_isnil( L, index ) )
				values[ i ] = 0;
			else if ( lua_type( L, index ) == LUA_TSTRING )
				values[ i ] = reinterpret_cast< word_t >( lua_tostring( L, index ) );
			else
				values[ i ] = static_cast< word_t >( luaL_checkinteger( L, index ) );
		}

		if ( !function )
			return luaL_error( L, "mem.call: null function" );

		word_t result = 0;
		if ( !seh_invoke( k_invokers[ args ], function, values, &result ) )
			return luaL_error( L, "mem.call: crashed inside %p", reinterpret_cast< void* >( function ) );

		lua_pushinteger( L, static_cast< lua_Integer >( result ) );
		return 1;
	}

	int l_mem_alloc( lua_State* L )
	{
		const std::size_t size = static_cast< std::size_t >( std::clamp< lua_Integer >( luaL_checkinteger( L, 1 ), 1, 64 << 20 ) );
		void* memory           = std::calloc( 1, size );
		if ( !memory )
			return luaL_error( L, "mem.alloc: out of memory" );

		n_lua_api::script_of( L )->m_allocations.push_back( memory );
		push_address( L, memory );
		return 1;
	}

	int l_mem_free( lua_State* L )
	{
		void* memory      = reinterpret_cast< void* >( check_address( L, 1 ) );
		auto& allocations = n_lua_api::script_of( L )->m_allocations;

		const auto it = std::find( allocations.begin( ), allocations.end( ), memory );
		if ( it == allocations.end( ) )
			return luaL_error( L, "mem.free: not a mem.alloc block" );

		std::free( memory );
		allocations.erase( it );
		return 0;
	}

	int l_mem_protect( lua_State* L )
	{
		DWORD old_protect = 0;
		if ( !VirtualProtect( reinterpret_cast< void* >( check_address( L, 1 ) ), static_cast< SIZE_T >( luaL_checkinteger( L, 2 ) ),
		                      static_cast< DWORD >( luaL_checkinteger( L, 3 ) ), &old_protect ) ) {
			lua_pushnil( L );
			return 1;
		}

		lua_pushinteger( L, old_protect );
		return 1;
	}

	int l_mem_netvar( lua_State* L )
	{
		lua_pushinteger( L, netvar_offset( luaL_checkstring( L, 1 ) ) );
		return 1;
	}

	int l_mem_f2i( lua_State* L )
	{
		const float value  = check_float( L, 1 );
		std::uint32_t bits = 0;
		std::memcpy( &bits, &value, sizeof( bits ) );
		lua_pushinteger( L, bits );
		return 1;
	}

	int l_mem_i2f( lua_State* L )
	{
		const std::uint32_t bits = static_cast< std::uint32_t >( luaL_checkinteger( L, 1 ) );
		float value              = 0.f;
		std::memcpy( &value, &bits, sizeof( value ) );
		lua_pushnumber( L, value );
		return 1;
	}

	int l_mem_interfaces( lua_State* L )
	{
		const std::pair< const char*, const void* > list[ ] = {
			{ "base_client", I::Client },
			{ "engine_client", I::EngineClient },
			{ "client_entity_list", I::ClientEntityList },
			{ "engine_trace", I::EngineTrace },
			{ "convar", I::CVar },
			{ "model_info", I::ModelInfoClient },
			{ "game_movement", I::GameMovement },
			{ "prediction", I::Prediction },
			{ "move_helper", I::MoveHelper },
			{ "global_vars", I::GlobalVars },
			{ "client_mode", I::ClientModeShared },
			{ "client_state", I::ClientState },
			{ "input", I::Input },
			{ "game_event_manager", I::GameEventManager },
			{ "engine_vgui", I::EngineVGui },
			{ "surface", I::MatSystemSurface },
			{ "panel", I::Panel },
			{ "render_view", I::RenderView },
			{ "key_values_system", I::KeyValuesSystem },
			{ "physics", I::Physics },
			{ "physics_collision", I::PhysicsCollision },
			{ "mem_alloc", I::MemAlloc },
			{ "hud_chat", I::ClientModeShared ? I::ClientModeShared->m_pChatElement : nullptr },
		};

		lua_createtable( L, 0, static_cast< int >( std::size( list ) ) );
		for ( const auto& [ name, pointer ] : list ) {
			push_address( L, pointer );
			lua_setfield( L, -2, name );
		}
		return 1;
	}

	/* ---- client ---- */

	int l_client_log( lua_State* L )
	{
		const std::string prefix = "[" + n_lua_api::script_of( L )->m_name + "]";
		LuaB_Log( prefix.c_str( ), luaL_checkstring( L, 1 ) );
		return 0;
	}

	/* Aim_ChatPrint: the text IS the ChatPrintf format, so '%' goes */
	int l_client_chat( lua_State* L )
	{
		if ( !sdk_ready( ) )
			return 0;

		std::string text = luaL_checkstring( L, 1 );
		std::replace( text.begin( ), text.end( ), '%', ' ' );
		Aim_ChatPrint( text.c_str( ) );
		return 0;
	}

	int l_client_name( lua_State* L )
	{
		lua_pushstring( L, n_lua_api::script_of( L )->m_name.c_str( ) );
		return 1;
	}

	int l_client_folder( lua_State* L )
	{
		lua_pushstring( L, LuaB_Folder( ) );
		return 1;
	}

	int l_client_unload( lua_State* L )
	{
		g_lua.request( n_lua::request_unload, n_lua_api::script_of( L )->m_name );
		return 0;
	}

	int l_print( lua_State* L )
	{
		std::string line;
		const int count = lua_gettop( L );
		for ( int i = 1; i <= count; i++ ) {
			std::size_t length = 0;
			const char* text   = luaL_tolstring( L, i, &length );
			if ( i > 1 )
				line += '\t';
			line.append( text, length );
			lua_pop( L, 1 );
		}

		n_lua_api::console_msg( "[" + n_lua_api::script_of( L )->m_name + "] " + line );
		return 0;
	}

	/* ---- cmd ---- */

	CUserCmd* check_cmd( lua_State* L )
	{
		luaL_checkudata( L, 1, k_cmd_meta );
		if ( !n_lua_api::s_cmd )
			luaL_error( L, "cmd is only valid inside its create_move / pre_prediction callback" );
		return static_cast< CUserCmd* >( n_lua_api::s_cmd );
	}

	int l_cmd_index( lua_State* L )
	{
		CUserCmd* cmd              = check_cmd( L );
		const std::string_view key = luaL_checkstring( L, 2 );

		if ( key == "command_number" ) lua_pushinteger( L, cmd->command_number );
		else if ( key == "tick_count" ) lua_pushinteger( L, cmd->tick_count );
		else if ( key == "pitch" ) lua_pushnumber( L, cmd->viewangles.x );
		else if ( key == "yaw" ) lua_pushnumber( L, cmd->viewangles.y );
		else if ( key == "roll" ) lua_pushnumber( L, cmd->viewangles.z );
		else if ( key == "forwardmove" ) lua_pushnumber( L, cmd->forwardmove );
		else if ( key == "sidemove" ) lua_pushnumber( L, cmd->sidemove );
		else if ( key == "upmove" ) lua_pushnumber( L, cmd->upmove );
		else if ( key == "buttons" ) lua_pushinteger( L, cmd->buttons );
		else if ( key == "impulse" ) lua_pushinteger( L, cmd->impulse );
		else if ( key == "weaponselect" ) lua_pushinteger( L, cmd->weaponselect );
		else if ( key == "weaponsubtype" ) lua_pushinteger( L, cmd->weaponsubtype );
		else if ( key == "random_seed" ) lua_pushinteger( L, cmd->random_seed );
		else if ( key == "mousedx" ) lua_pushinteger( L, cmd->mousedx );
		else if ( key == "mousedy" ) lua_pushinteger( L, cmd->mousedy );
		else if ( key == "has_been_predicted" ) lua_pushboolean( L, cmd->hasbeenpredicted );
		else if ( key == "send_packet" ) lua_pushboolean( L, n_lua_api::s_send_packet ? *n_lua_api::s_send_packet : true );
		else if ( key == "ptr" ) push_address( L, cmd );
		else lua_pushnil( L );
		return 1;
	}

	int l_cmd_newindex( lua_State* L )
	{
		CUserCmd* cmd              = check_cmd( L );
		const std::string_view key = luaL_checkstring( L, 2 );

		if ( key == "command_number" ) cmd->command_number = static_cast< int >( luaL_checkinteger( L, 3 ) );
		else if ( key == "tick_count" ) cmd->tick_count = static_cast< int >( luaL_checkinteger( L, 3 ) );
		else if ( key == "pitch" ) cmd->viewangles.x = check_float( L, 3 );
		else if ( key == "yaw" ) cmd->viewangles.y = check_float( L, 3 );
		else if ( key == "roll" ) cmd->viewangles.z = check_float( L, 3 );
		else if ( key == "forwardmove" ) cmd->forwardmove = check_float( L, 3 );
		else if ( key == "sidemove" ) cmd->sidemove = check_float( L, 3 );
		else if ( key == "upmove" ) cmd->upmove = check_float( L, 3 );
		else if ( key == "buttons" ) cmd->buttons = static_cast< int >( luaL_checkinteger( L, 3 ) );
		else if ( key == "impulse" ) cmd->impulse = static_cast< unsigned char >( luaL_checkinteger( L, 3 ) );
		else if ( key == "weaponselect" ) cmd->weaponselect = static_cast< int >( luaL_checkinteger( L, 3 ) );
		else if ( key == "weaponsubtype" ) cmd->weaponsubtype = static_cast< int >( luaL_checkinteger( L, 3 ) );
		else if ( key == "random_seed" ) cmd->random_seed = static_cast< int >( luaL_checkinteger( L, 3 ) );
		else if ( key == "mousedx" ) cmd->mousedx = static_cast< short >( luaL_checkinteger( L, 3 ) );
		else if ( key == "mousedy" ) cmd->mousedy = static_cast< short >( luaL_checkinteger( L, 3 ) );
		else if ( key == "send_packet" ) {
			if ( n_lua_api::s_send_packet )
				*n_lua_api::s_send_packet = lua_toboolean( L, 3 );
		} else
			return luaL_error( L, "cmd has no writable field '%s'", std::string( key ).c_str( ) );
		return 0;
	}

	/* ---- event ---- */

	IGameEvent* check_event( lua_State* L )
	{
		luaL_checkudata( L, 1, k_event_meta );
		if ( !n_lua_api::s_event )
			luaL_error( L, "event is only valid inside its callback" );
		return static_cast< IGameEvent* >( n_lua_api::s_event );
	}

	int l_event_name( lua_State* L )
	{
		const char* name = check_event( L )->GetName( );
		lua_pushstring( L, name ? name : "" );
		return 1;
	}

	int l_event_get_int( lua_State* L )
	{
		lua_pushinteger( L, check_event( L )->GetInt( luaL_checkstring( L, 2 ), static_cast< int >( luaL_optinteger( L, 3, 0 ) ) ) );
		return 1;
	}

	int l_event_get_float( lua_State* L )
	{
		lua_pushnumber( L, check_event( L )->GetFloat( luaL_checkstring( L, 2 ), opt_float( L, 3, 0.f ) ) );
		return 1;
	}

	int l_event_get_bool( lua_State* L )
	{
		lua_pushboolean( L, check_event( L )->GetBool( luaL_checkstring( L, 2 ), lua_toboolean( L, 3 ) ) );
		return 1;
	}

	int l_event_get_string( lua_State* L )
	{
		const char* value = check_event( L )->GetString( luaL_checkstring( L, 2 ), luaL_optstring( L, 3, "" ) );
		lua_pushstring( L, value ? value : "" );
		return 1;
	}

	int l_event_get_player( lua_State* L )
	{
		IGameEvent* event = check_event( L );
		require_sdk( L );
		const int user_id = event->GetInt( luaL_optstring( L, 2, "userid" ), 0 );
		const int index   = I::EngineClient->GetPlayerForUserID( user_id );
		push_entity( L, index > 0 ? entity_at( index ) : nullptr );
		return 1;
	}

	/* ---- registration ---- */

	void new_meta( lua_State* L, const char* name, const luaL_Reg* methods, const luaL_Reg* metamethods )
	{
		luaL_newmetatable( L, name );
		if ( metamethods )
			luaL_setfuncs( L, metamethods, 0 );
		if ( methods ) {
			lua_newtable( L );
			luaL_setfuncs( L, methods, 0 );
			lua_setfield( L, -2, "__index" );
		}
		lua_pushstring( L, "locked" );
		lua_setfield( L, -2, "__metatable" );
		lua_pop( L, 1 );
	}

	void add_constants( lua_State* L, const char* name, const std::pair< const char*, lua_Integer >* values, const std::size_t count )
	{
		lua_createtable( L, 0, static_cast< int >( count ) );
		for ( std::size_t i = 0; i < count; i++ ) {
			lua_pushinteger( L, values[ i ].second );
			lua_setfield( L, -2, values[ i ].first );
		}
		lua_setglobal( L, name );
	}
}

void n_lua_api::console_msg( const std::string& text )
{
	LuaB_Console( text.c_str( ) );
}

int n_lua_api::traceback( lua_State* L )
{
	const char* message = lua_tostring( L, 1 );
	if ( !message )
		message = luaL_tolstring( L, 1, nullptr );

	luaL_traceback( L, L, message, 1 );
	return 1;
}

void n_lua_api::push_cmd( lua_State* L )
{
	lua_newuserdatauv( L, 1, 0 );
	luaL_setmetatable( L, k_cmd_meta );
}

void n_lua_api::push_event( lua_State* L )
{
	lua_newuserdatauv( L, 1, 0 );
	luaL_setmetatable( L, k_event_meta );
}

const char* n_lua_api::event_name( void* event )
{
	return event ? static_cast< IGameEvent* >( event )->GetName( ) : nullptr;
}

void n_lua_api::commit_draws( )
{
	float w = 0.f, h = 0.f;
	screen_size( w, h );
	LuaDraw::Commit( w, h, GetTickCount64( ) );
}

void n_lua_api::open( lua_State* L )
{
	lua_newtable( L );
	lua_setfield( L, LUA_REGISTRYINDEX, "botox.callbacks" );

	/* require( "name" ) -> C:\botox-tf2\lua\lib\name.lua ( lib is not scanned, so modules never show as scripts ) */
	if ( lua_getglobal( L, "package" ) == LUA_TTABLE ) {
		const std::string folder = LuaB_Folder( );
		lua_pushstring( L, ( folder + "\\lib\\?.lua;" + folder + "\\lib\\?\\init.lua" ).c_str( ) );
		lua_setfield( L, -2, "path" );
	}
	lua_pop( L, 1 );

	lua_pushcfunction( L, l_print );
	lua_setglobal( L, "print" );

	lua_pushcfunction( L, l_color );
	lua_setglobal( L, "color" );

	static const luaL_Reg callbacks[ ] = { { "register", l_callbacks_register }, { "unregister", l_callbacks_unregister }, { nullptr, nullptr } };
	add_table( L, "callbacks", callbacks );

	static const luaL_Reg ui[ ] = { { "checkbox", l_ui_checkbox }, { "slider_int", l_ui_slider_int }, { "slider_float", l_ui_slider_float },
		                            { "combo", l_ui_combo },       { "color", l_ui_color },           { "keybind", l_ui_keybind },
		                            { "button", l_ui_button },     { "text", l_ui_text },             { "input", l_ui_input },
		                            { nullptr, nullptr } };
	add_table( L, "ui", ui );

	static const luaL_Reg element_methods[ ] = { { "get", l_element_get },
		                                         { "set", l_element_set },
		                                         { "visible", l_element_visible },
		                                         { "label", l_element_label },
		                                         { nullptr, nullptr } };
	new_meta( L, k_element_meta, element_methods, nullptr );

	static const luaL_Reg render[ ] = { { "screen_size", l_render_screen_size },
		                                { "text", l_render_text },
		                                { "text_size", l_render_text_size },
		                                { "line", l_render_line },
		                                { "rect", l_render_rect },
		                                { "rect_filled", l_render_rect_filled },
		                                { "gradient", l_render_gradient },
		                                { "circle", l_render_circle },
		                                { "circle_filled", l_render_circle_filled },
		                                { "triangle", l_render_triangle },
		                                { "poly", l_render_poly },
		                                { "world_to_screen", l_render_world_to_screen },
		                                { nullptr, nullptr } };
	add_table( L, "render", render );

	static const luaL_Reg entities[ ] = { { "local_player", l_entities_local },
		                                  { "get", l_entities_get },
		                                  { "from_handle", l_entities_from_handle },
		                                  { "highest_index", l_entities_highest_index },
		                                  { "players", l_entities_players },
		                                  { "by_class", l_entities_by_class },
		                                  { nullptr, nullptr } };
	add_table( L, "entities", entities );

	static const luaL_Reg entity_methods[ ] = { { "index", l_entity_index },
		                                        { "ptr", l_entity_ptr },
		                                        { "is_valid", l_entity_is_valid },
		                                        { "is_player", l_entity_is_player },
		                                        { "is_alive", l_entity_is_alive },
		                                        { "is_dormant", l_entity_is_dormant },
		                                        { "is_enemy", l_entity_is_enemy },
		                                        { "team", l_entity_team },
		                                        { "health", l_entity_health },
		                                        { "armor", l_entity_armor },
		                                        { "flags", l_entity_flags },
		                                        { "move_type", l_entity_move_type },
		                                        { "origin", l_entity_origin },
		                                        { "velocity", l_entity_velocity },
		                                        { "eye_position", l_entity_eye_position },
		                                        { "mins_maxs", l_entity_mins_maxs },
		                                        { "class_name", l_entity_class_name },
		                                        { "class_id", l_entity_class_id },
		                                        { "name", l_entity_name },
		                                        { "weapon", l_entity_weapon },
		                                        { "weapon_id", l_entity_weapon_id },
		                                        { "bone_position", l_entity_bone_position },
		                                        { "hitbox_position", l_entity_hitbox_position },
		                                        { "get_prop", l_entity_get_prop },
		                                        { "set_prop", l_entity_set_prop },
		                                        { "get_datafield", l_entity_get_datafield },
		                                        { "set_datafield", l_entity_set_datafield },
		                                        { nullptr, nullptr } };
	static const luaL_Reg entity_meta[ ]    = { { "__eq", l_entity_eq }, { "__tostring", l_entity_tostring }, { nullptr, nullptr } };
	new_meta( L, k_entity_meta, entity_methods, entity_meta );

	static const luaL_Reg engine[ ] = { { "exec", l_engine_exec },
		                                { "in_game", l_engine_in_game },
		                                { "connected", l_engine_connected },
		                                { "local_index", l_engine_local_index },
		                                { "max_clients", l_engine_max_clients },
		                                { "map_name", l_engine_map_name },
		                                { "level_name", l_engine_level_name },
		                                { "view_angles", l_engine_view_angles },
		                                { "set_view_angles", l_engine_set_view_angles },
		                                { "player_info", l_engine_player_info },
		                                { "player_for_user_id", l_engine_player_for_user_id },
		                                { "playing_demo", l_engine_playing_demo },
		                                { "latency", l_engine_latency },
		                                { nullptr, nullptr } };
	add_table( L, "engine", engine );

	static const luaL_Reg globals[ ] = { { "realtime", l_globals_realtime },         { "curtime", l_globals_curtime },
		                                 { "frametime", l_globals_frametime },       { "absframetime", l_globals_absframetime },
		                                 { "tickcount", l_globals_tickcount },       { "tickinterval", l_globals_tickinterval },
		                                 { "framecount", l_globals_framecount },     { "maxclients", l_globals_maxclients },
		                                 { nullptr, nullptr } };
	add_table( L, "globals", globals );

	static const luaL_Reg cvar[ ] = { { "exists", l_cvar_exists },         { "get_int", l_cvar_get_int }, { "get_float", l_cvar_get_float },
		                              { "get_string", l_cvar_get_string }, { "set", l_cvar_set },         { nullptr, nullptr } };
	add_table( L, "cvar", cvar );

	static const luaL_Reg config[ ] = { { "get", l_config_get },   { "set", l_config_set },   { "type", l_config_type },
		                                { "reset", l_config_reset }, { "list", l_config_list }, { nullptr, nullptr } };
	add_table( L, "config", config );

	static const luaL_Reg trace[ ] = { { "line", l_trace_line }, { "hull", l_trace_hull }, { "point_contents", l_trace_point_contents }, { nullptr, nullptr } };
	add_table( L, "trace", trace );

	static const luaL_Reg input[ ] = { { "is_key_down", l_input_is_key_down },
		                               { "mouse_pos", l_input_mouse_pos },
		                               { "menu_open", l_input_menu_open },
		                               { "keys_blocked", l_input_keys_blocked },
		                               { nullptr, nullptr } };
	add_table( L, "input", input );

	static const luaL_Reg mem[ ] = { { "module", l_mem_module },
		                             { "find_pattern", l_mem_find_pattern },
		                             { "interface", l_mem_interface },
		                             { "export", l_mem_export },
		                             { "interfaces", l_mem_interfaces },
		                             { "netvar", l_mem_netvar },
		                             { "read_i8", l_mem_read< std::int8_t > },
		                             { "read_u8", l_mem_read< std::uint8_t > },
		                             { "read_i16", l_mem_read< std::int16_t > },
		                             { "read_u16", l_mem_read< std::uint16_t > },
		                             { "read_i32", l_mem_read< std::int32_t > },
		                             { "read_u32", l_mem_read< std::uint32_t > },
		                             { "read_i64", l_mem_read< std::int64_t > },
		                             { "read_float", l_mem_read< float > },
		                             { "read_double", l_mem_read< double > },
		                             { "read_ptr", l_mem_read< word_t > },
		                             { "read_string", l_mem_read_string },
		                             { "read_bytes", l_mem_read_bytes },
		                             { "write_i8", l_mem_write< std::int8_t > },
		                             { "write_u8", l_mem_write< std::uint8_t > },
		                             { "write_i16", l_mem_write< std::int16_t > },
		                             { "write_u16", l_mem_write< std::uint16_t > },
		                             { "write_i32", l_mem_write< std::int32_t > },
		                             { "write_u32", l_mem_write< std::uint32_t > },
		                             { "write_i64", l_mem_write< std::int64_t > },
		                             { "write_float", l_mem_write< float > },
		                             { "write_double", l_mem_write< double > },
		                             { "write_ptr", l_mem_write< word_t > },
		                             { "write_string", l_mem_write_string },
		                             { "write_bytes", l_mem_write_bytes },
		                             { "vfunc", l_mem_vfunc },
		                             { "call", l_mem_call },
		                             { "alloc", l_mem_alloc },
		                             { "free", l_mem_free },
		                             { "protect", l_mem_protect },
		                             { "f2i", l_mem_f2i },
		                             { "i2f", l_mem_i2f },
		                             { nullptr, nullptr } };
	add_table( L, "mem", mem );

	static const luaL_Reg client[ ] = { { "log", l_client_log },       { "chat", l_client_chat },     { "name", l_client_name },
		                                { "folder", l_client_folder }, { "unload", l_client_unload }, { nullptr, nullptr } };
	add_table( L, "client", client );

	static const luaL_Reg cmd_meta[ ] = { { "__index", l_cmd_index }, { "__newindex", l_cmd_newindex }, { nullptr, nullptr } };
	new_meta( L, k_cmd_meta, nullptr, cmd_meta );

	static const luaL_Reg event_methods[ ] = { { "name", l_event_name },           { "get_int", l_event_get_int },
		                                       { "get_float", l_event_get_float }, { "get_bool", l_event_get_bool },
		                                       { "get_string", l_event_get_string }, { "get_player", l_event_get_player },
		                                       { nullptr, nullptr } };
	new_meta( L, k_event_meta, event_methods, nullptr );

	/* source sdk 2013 in_buttons.h / const.h / bspflags.h, same numbers as csgo */
	static const std::pair< const char*, lua_Integer > buttons[ ] = {
		{ "attack", 1 << 0 },     { "jump", 1 << 1 },      { "duck", 1 << 2 },        { "forward", 1 << 3 },  { "back", 1 << 4 },
		{ "use", 1 << 5 },        { "left", 1 << 7 },      { "right", 1 << 8 },       { "moveleft", 1 << 9 }, { "moveright", 1 << 10 },
		{ "attack2", 1 << 11 },   { "reload", 1 << 13 },   { "score", 1 << 16 },      { "speed", 1 << 17 },   { "walk", 1 << 18 },
		{ "zoom", 1 << 19 },      { "bullrush", 1 << 22 }, { "attack3", 1 << 25 },
	};
	add_constants( L, "buttons", buttons, std::size( buttons ) );

	static const std::pair< const char*, lua_Integer > flags[ ] = {
		{ "onground", 1 << 0 }, { "ducking", 1 << 1 },  { "animducking", 1 << 2 }, { "waterjump", 1 << 3 },      { "frozen", 1 << 6 },
		{ "inwater", 1 << 10 }, { "fakeclient", 1 << 9 }, { "godmode", 1 << 15 },  { "partialground", 1 << 18 },
	};
	add_constants( L, "flags", flags, std::size( flags ) );

	static const std::pair< const char*, lua_Integer > stages[ ] = {
		{ "start", 0 },          { "net_update_start", 1 }, { "net_update_postdataupdate_start", 2 }, { "net_update_postdataupdate_end", 3 },
		{ "net_update_end", 4 }, { "render_start", 5 },     { "render_end", 6 },
	};
	add_constants( L, "stages", stages, std::size( stages ) );

	static const std::pair< const char*, lua_Integer > fonts[ ] = {
		{ "verdana", LUAF_VERDANA },
		{ "verdana_bold", LUAF_VERDANA_BOLD },
		{ "tahoma", LUAF_TAHOMA },
		{ "tahoma_bold", LUAF_TAHOMA_BOLD },
		{ "icons", LUAF_ICONS },
		{ "indicator", LUAF_INDICATOR },
		{ "custom_indicator", LUAF_CUSTOM_INDICATOR },
		{ "custom_esp", LUAF_CUSTOM_ESP },
		{ "custom_net_graph", LUAF_CUSTOM_NET_GRAPH },
	};
	add_constants( L, "fonts", fonts, std::size( fonts ) );

	static const std::pair< const char*, lua_Integer > text_flags[ ] = { { "none", 0 }, { "shadow", 1 }, { "outline", 2 } };
	add_constants( L, "text_flags", text_flags, std::size( text_flags ) );

	static const std::pair< const char*, lua_Integer > masks[ ] = { { "all", 0xFFFFFFFF },
		                                                             { "solid", 0x200400B },
		                                                             { "playersolid", 0x201400B },
		                                                             { "shot", 0x46004003 } };
	add_constants( L, "masks", masks, std::size( masks ) );
}
