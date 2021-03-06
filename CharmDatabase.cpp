#include "stdafx.h"
#include "CharmDatabase.h"
#include "Solution.h"
#include "Armor.h"
#include "Skill.h"
#include <fstream>
#include <cmath>

using namespace System;

unsigned CalcMaxCharmType( Query^ query )
{
	const int hr_to_charm_type[] = 
	{
		0, // HR0+ mystery
		0,
		0,
		1, // HR3+ shining
		1, 
		2, // HR5+ timeworn
		2,
		2,
		2, // HR8+ (special permit)
		2, // G1*
		3,
		3,
		3, //G4*
		3, //All
	};

	return hr_to_charm_type[ query->hr ];
}

String^ FixTypos( String^ input )
{
	return input;
}

#pragma region Custom charms/gear

#define CUSTOM_CHARM_TXT L"Data/mycharms.txt"

void CharmDatabase::SaveCustom()
{
	//slots,skill1,num,skill2,num
	IO::StreamWriter fout( CUSTOM_CHARM_TXT );
	fout.WriteLine( L"#Format: Slots,Skill1,Points1,Skill2,Points2" );
	for each( Charm^ ch in mycharms )
	{
		fout.Write( Convert::ToString( ch->num_slots ) );
		for( int i = 0; i < 2; ++i )
		{
			if( i < ch->abilities.Count )
				fout.Write( L"," + ch->abilities[ i ]->ability->name + L"," + Convert::ToString( ch->abilities[ i ]->amount ) );
			else fout.Write( L",," );
		}
		fout.WriteLine();
	}
}

bool CharmDatabase::CreateNewCustom()
{
	if( !IO::File::Exists( CUSTOM_CHARM_TXT ) )
	{
		mycharms.Clear();
		Charm^ charm = gcnew Charm( 0 );
		charm->abilities.Add( gcnew AbilityPair( SpecificAbility::gathering, 10 ) );
		charm->custom = true;
		mycharms.Add( charm );
		return true;
	}
	return false;
}

List_t< Charm^ >^ CharmDatabase::LoadCharms( System::String^ filename )
{
	List_t< Charm^ >^ results = gcnew List_t< Charm^ >();

	IO::StreamReader fin( filename );
	String^ temp;
	while( !fin.EndOfStream )
	{
		temp = fin.ReadLine();
		if( temp == L"" || temp[ 0 ] == L'#' ) continue;
		List_t< String^ > split;
		Utility::SplitString( %split, temp, L',' );
		if( split.Count != 5 )
		{
			//MessageBox::Show( L"Failed to load mycharms.txt: Wrong number of commas" );
			results->Clear();
			return results;
		}

		//slots,skill1,num,skill2,num
		Charm^ charm = gcnew Charm();
		charm->num_slots = Convert::ToUInt32( split[ 0 ] );

		try
		{
			if( split[ 1 ] != L"" )
			{
				if( StringTable::english )
					split[ 1 ] = FixTypos( split[ 1 ] );
				Ability^ ab = Ability::FindAbility( split[ 1 ] );
				if( !ab )
				{
					results->Clear();
					return results;
				}

				charm->abilities.Add( gcnew AbilityPair( ab, Convert::ToInt32( split[ 2 ] ) ) );
			}
			if( split[ 3 ] != L"" )
			{
				if( StringTable::english )
					split[ 3 ] = FixTypos( split[ 3 ] );
				Ability^ ab = Ability::FindAbility( split[ 3 ] );
				if( !ab )
				{
					results->Clear();
					return results;
				}

				charm->abilities.Add( gcnew AbilityPair( ab, Convert::ToInt32( split[ 4 ] ) ) );
			}
		}
		catch( Exception^ )
		{
			results->Clear();
			return results;
		}
		results->Add( charm );
	}

	return results;
}

bool CharmDatabase::LoadCustom()
{
	if( !IO::File::Exists( CUSTOM_CHARM_TXT ) )
	{
		Charm^ charm = gcnew Charm( 0 );
		charm->abilities.Add( gcnew AbilityPair( SpecificAbility::gathering, 10 ) );
		mycharms.Clear();
		mycharms.Add( charm );
		return true;
	}

	List_t< Charm^ >^ charms = LoadCharms( CUSTOM_CHARM_TXT );
	if( charms->Count == 0 )
		return true;

	mycharms.Clear();

	bool cheats = false;
	for each( Charm^ charm in charms )
	{
		if( CharmDatabase::CharmIsLegal( charm ) )
		{
			charm->custom = true;
			mycharms.Add( charm );
		}
		else
		{
			cheats = true;
		}
	}

	if( cheats )
		System::Windows::Forms::MessageBox::Show( StaticString( Cheater ) );

	return true;
}

#pragma endregion

#pragma region Charm Generation
ref struct StaticData
{
	static array< array< unsigned char >^ >^ skill1_table;
	static array< array<   signed char >^ >^ skill2_table; // [type][skill]
	static array< array< unsigned char >^ >^ slot_table;

	static array< unsigned char >^ skill2_chance_table =
	{
		100,
		35,
		25,
		20,
		15,
		25
	};
};

int rnd( const int n )
{
	Assert( n < 65536 && n >= 0, L"Bad RND" );
	if( n == 0 ) return 176;

	return ( n * 176 ) % 65363;
}

int reverse_rnd( const int n )
{
	return ( n * 7799 ) % 65363;
}

unsigned GetNumSlots( const unsigned charm_type, const int slot_table, const unsigned roll )
{
	Assert( (int)charm_type < StaticData::slot_table->Length, L"Bad charm type" );

	const unsigned table_index = Math::Min( slot_table, StaticData::slot_table[ charm_type ]->Length / 3 ) - 1;
	const unsigned for2 = StaticData::slot_table[ charm_type ][ table_index * 3 + 1 ];
	const unsigned for3 = StaticData::slot_table[ charm_type ][ table_index * 3 + 2 ];
	const unsigned for1 = StaticData::slot_table[ charm_type ][ table_index * 3 + 0 ];
	if( roll >= for2 )
	{
		return ( roll >= for3 ) ? 3 : 2;
	}
	else
	{
		return ( roll >= for1 ) ? 1 : 0;
	}
}

bool TryTwoSkillCharm( const unsigned charm_type, int n, int m, array< List_t< unsigned >^ >^ charms )
{
	array< unsigned char >^ skill1_table = StaticData::skill1_table[ charm_type ];
	array< signed char >^	skill2_table = StaticData::skill2_table[ charm_type ];
	const unsigned num_skills1 = skill1_table->Length / 3;
	const unsigned num_skills2 = skill2_table == nullptr ? 0 : skill2_table->Length / 3;

	//skill 1
	const int skill1_index = n % num_skills1;
	const int skill1_name = skill1_table[ skill1_index * 3 ];
	const int skill1_min = skill1_table[ skill1_index * 3 + 1 ];
	const int skill1_max = skill1_table[ skill1_index * 3 + 2 ];

	//skill 1 point
	n = rnd( n );
	const int point1 = skill1_min + (n^m) % ( skill1_max - skill1_min + 1 );

	//has skill 2?
	int skill2_index = -1, point2 = 0, skill2_min = 0, skill2_max = 0, skill2_name = 0;
	n = rnd( n );
	m = rnd( m );
	if( (n^m) % 100 >= StaticData::skill2_chance_table[ charm_type ] )
	{
		//skill 2
		m = rnd( m );
		skill2_index = m % num_skills2;
		skill2_name = skill2_table[ skill2_index * 3 ];
		skill2_min = skill2_table[ skill2_index * 3 + 1 ];
		skill2_max = skill2_table[ skill2_index * 3 + 2 ];

		//skill 2 point
		if( skill2_min > 0 ) //positive number
		{
			n = rnd( n );
			m = rnd( m );
			point2 = ( (n^m) % ( skill2_max - skill2_min + 1 ) ) + skill2_min;
		}
		else //check for negative
		{
			n = rnd( n );
			if( n % 2 == 1 ) //positive
			{
				n = rnd( n );
				m = rnd( m );
				point2 = (n^m) % skill2_max + 1;
			}
			else //negative
			{
				n = rnd( n );
				m = rnd( m );
				point2 = skill2_min + (n^m) % ( -skill2_min );
			}
		}
		if( point2 < 0 )
			return false;

		if( skill1_name == skill2_name )
		{
			skill2_min = skill2_max = point2 = 0;
			skill2_name = skill2_index = Ability::static_abilities.Count;
		}
	}

	const int slot_table = (int)Math::Floor( point1*10.0 / skill1_max + ( (point2 > 0) ? point2*10.0 / skill2_max : 0 ) );

	//slots
	n = rnd( n );
	int slot_roll = 0;
	if( n & 128 )
	{
		n = rnd( n );
		slot_roll = n % 100;
	}
	else
	{
		m = rnd( m );
		slot_roll = m % 100;
	}

	const int num_slots = GetNumSlots( charm_type, slot_table, slot_roll );

	List_t< unsigned >^ list = charms[ num_slots ];
	for( int i = 0; i < list->Count; ++i )
	{
		const unsigned hash = list[i];
		const int p1 = hash >> 16;
		const int p2 = hash & 0xFFFF;
		if( p1 >= point1 && p2 >= point2 )
			return false;
	}
	list->Add( point1 << 16 | point2 );

	return num_slots == Math::Min( 3u, charm_type + 1 ) && slot_table == 20;
}

String^ CanGenerateCharm1( const unsigned charm_type, int n, int m, Charm^ charm )
{
	Assert( charm->abilities.Count == 1, "Too many abilities for charm" );
	Assert( (int)charm_type < StaticData::skill1_table->Length && (int)charm_type < StaticData::skill2_table->Length, "Charm type is invalid" );
	array< unsigned char >^ skill1_table = StaticData::skill1_table[ charm_type ];
	array< signed char >^	skill2_table = StaticData::skill2_table[ charm_type ];
	const unsigned num_skills1 = skill1_table->Length / 3;
	const unsigned num_skills2 = skill2_table->Length / 3;

	//skill 1
	const int skill1_index = n % num_skills1;
	const int skill1_name = skill1_table[ skill1_index * 3 ];
	const int skill1_min = skill1_table[ skill1_index * 3 + 1 ];
	const int skill1_max = skill1_table[ skill1_index * 3 + 2 ];

	//skill 1 point
	n = rnd( n );
	const int point1 = skill1_min + (n^m) % ( skill1_max - skill1_min + 1 );

	if( skill1_name == charm->abilities[0]->ability->static_index && point1 < charm->abilities[0]->amount )
		return nullptr;

	//has skill 2?
	int skill2_index = -1, point2 = 0, skill2_min = 0, skill2_max = 0, skill2_name = 0;
	n = rnd( n );
	m = rnd( m );
	if( (n^m) % 100 >= StaticData::skill2_chance_table[ charm_type ] )
	{
		//skill 2
		m = rnd( m );
		skill2_index = m % num_skills2;
		skill2_name = skill2_table[ skill2_index * 3 ];
		skill2_min = skill2_table[ skill2_index * 3 + 1 ];
		skill2_max = skill2_table[ skill2_index * 3 + 2 ];

		//skill 2 point
		if( skill2_min > 0 ) //positive number
		{
			n = rnd( n );
			m = rnd( m );
			point2 = ( (n^m) % ( skill2_max - skill2_min + 1 ) ) + skill2_min;
		}
		else //check for negative
		{
			n = rnd( n );
			if( n % 2 == 1 ) //positive
			{
				n = rnd( n );
				m = rnd( m );
				point2 = (n^m) % skill2_max + 1;
			}
			else //negative
			{
				n = rnd( n );
				m = rnd( m );
				point2 = skill2_min + (n^m) % ( -skill2_min );
			}
		}

		if( skill1_name == skill2_name )
		{
			skill2_name = point2 = 0;
			skill2_index = -1;
		}

		if( skill1_name != charm->abilities[0]->ability->static_index && skill2_name != charm->abilities[0]->ability->static_index ||
			point2 < charm->abilities[0]->amount )
			return nullptr;
	}

	const int slot_table = (int)Math::Floor( point1*10.0 / skill1_max + ( (point2 > 0) ? point2*10.0 / skill2_max : 0 ) );

	//slots
	n = rnd( n );
	int slot_roll = 0;
	if( n & 128 )
	{
		n = rnd( n );
		slot_roll = n % 100;
	}
	else
	{
		m = rnd( m );
		slot_roll = m % 100;
	}

	const unsigned num_slots = GetNumSlots( charm_type, slot_table, slot_roll );

	if( num_slots < charm->num_slots )
		return nullptr;

	Charm c( num_slots );
	c.abilities.Add( gcnew AbilityPair( Ability::static_abilities[ skill1_name ], point1 ) );
	if( point2 )
		c.abilities.Add( gcnew AbilityPair( Ability::static_abilities[ skill2_name ], point2 ) );

	return c.GetName();
}

String^ CanGenerateCharm2( const unsigned charm_type, int n, int m, Charm^ charm )
{
	Assert( charm->abilities.Count == 2, "Too few abilities for charm" );
	Assert( (int)charm_type < StaticData::skill1_table->Length && (int)charm_type < StaticData::skill2_table->Length, "Charm type is invalid" );
	array< unsigned char >^ skill1_table = StaticData::skill1_table[ charm_type ];
	array< signed char >^	skill2_table = StaticData::skill2_table[ charm_type ];
	const unsigned num_skills1 = skill1_table->Length / 3;
	const unsigned num_skills2 = skill2_table->Length / 3;

	//skill 1
	const int skill1_index = n % num_skills1;
	const int skill1_name = skill1_table[ skill1_index * 3 ];
	const int skill1_min = skill1_table[ skill1_index * 3 + 1 ];
	const int skill1_max = skill1_table[ skill1_index * 3 + 2 ];

	//skill 1 point
	n = rnd( n );
	const int point1 = skill1_min + (n^m) % ( skill1_max - skill1_min + 1 );

	if( point1 < charm->abilities[0]->amount )
		return nullptr;

	//has skill 2?
	int skill2_index = -1, point2 = 0, skill2_min = 0, skill2_max = 0, skill2_name = 0;
	n = rnd( n );
	m = rnd( m );
	if( (n^m) % 100 >= StaticData::skill2_chance_table[ charm_type ] )
	{
		//skill 2
		m = rnd( m );
		skill2_index = m % num_skills2;
		skill2_name = skill2_table[ skill2_index * 3 ];
		skill2_min = skill2_table[ skill2_index * 3 + 1 ];
		skill2_max = skill2_table[ skill2_index * 3 + 2 ];

		//skill 2 point
		if( skill2_min > 0 ) //positive number
		{
			n = rnd( n );
			m = rnd( m );
			point2 = ( (n^m) % ( skill2_max - skill2_min + 1 ) ) + skill2_min;
		}
		else //check for negative
		{
			n = rnd( n );
			if( n % 2 == 1 ) //positive
			{
				n = rnd( n );
				m = rnd( m );
				point2 = (n^m) % skill2_max + 1;
			}
			else //negative
			{
				n = rnd( n );
				m = rnd( m );
				point2 = skill2_min + (n^m) % ( -skill2_min );
			}
		}

		if( skill1_name == skill2_name || point2 < charm->abilities[1]->amount )
			return nullptr;
	}
	else return nullptr;

	const int slot_table = (int)Math::Floor( point1*10.0 / skill1_max + ( (point2 > 0) ? point2*10.0 / skill2_max : 0 ) );

	//slots
	n = rnd( n );
	int slot_roll = 0;
	if( n & 128 )
	{
		n = rnd( n );
		slot_roll = n % 100;
	}
	else
	{
		m = rnd( m );
		slot_roll = m % 100;
	}

	const unsigned num_slots = GetNumSlots( charm_type, slot_table, slot_roll );

	if( num_slots < charm->num_slots )
		return nullptr;

	Charm c( num_slots );
	c.abilities.Add( gcnew AbilityPair( Ability::static_abilities[ skill1_name ], point1 ) );
	c.abilities.Add( gcnew AbilityPair( Ability::static_abilities[ skill2_name ], point2 ) );

	return c.GetName();
}

bool CanGenerate2SkillCharm( const unsigned charm_type, int n, int m, Charm^ charm )
{
	Assert( charm->abilities.Count == 2, "Charm has too few abilities" );
	Assert( charm_type > 0 && (int)charm_type < StaticData::skill1_table->Length && (int)charm_type < StaticData::skill2_table->Length, "Charm type is invalid" );
	array< unsigned char >^ skill1_table = StaticData::skill1_table[ charm_type ];
	array< signed char >^	skill2_table = StaticData::skill2_table[ charm_type ];
	const unsigned num_skills1 = skill1_table->Length / 3;
	const unsigned num_skills2 = skill2_table->Length / 3;

	//skill 1
	//n = rnd( n );
	const int skill1_index = n % num_skills1;
	const int skill1_name = skill1_table[ skill1_index * 3 ];
	Assert( skill1_name == charm->abilities[ 0 ]->ability->static_index, "Skill1 failed" );
	const int skill1_min = skill1_table[ skill1_index * 3 + 1 ];
	const int skill1_max = skill1_table[ skill1_index * 3 + 2 ];

	//skill 1 point
	n = rnd( n );
	//m = rnd( m );
	const int point1 = skill1_min + (n^m) % ( skill1_max - skill1_min + 1 );

	if( point1 < charm->abilities[0]->amount )
		return false;

	//has skill 2?
	int skill2_index = -1, point2 = 0, skill2_min = 0, skill2_max = 0, skill2_name = 0;
	n = rnd( n );
	m = rnd( m );
	if( (n^m) % 100 >= StaticData::skill2_chance_table[ charm_type ] )
	{
		//skill 2
		m = rnd( m );
		skill2_index = m % num_skills2;
		skill2_name = (unsigned char)skill2_table[ skill2_index * 3 ];
		Assert( skill2_name == charm->abilities[1]->ability->static_index, "Skill2 failed" );
		skill2_min = skill2_table[ skill2_index * 3 + 1 ];
		skill2_max = skill2_table[ skill2_index * 3 + 2 ];

		//skill 2 point
		if( skill2_min > 0 ) //positive number
		{
			n = rnd( n );
			m = rnd( m );
			point2 = ( (n^m) % ( skill2_max - skill2_min + 1 ) ) + skill2_min;
		}
		else //check for negative
		{
			n = rnd( n );
			if( n % 2 == 1 ) //positive
			{
				n = rnd( n );
				m = rnd( m );
				point2 = (n^m) % skill2_max + 1;
			}
			else //negative
			{
				n = rnd( n );
				m = rnd( m );
				point2 = skill2_min + (n^m) % ( -skill2_min );
			}
		}

		if( skill1_name == skill2_name || point2 < charm->abilities[1]->amount )
		{
			return false;
		}
	}
	else return false;

	const int slot_table = (int)Math::Floor( point1*10.0 / skill1_max + ( (point2 > 0) ? point2*10.0 / skill2_max : 0 ) );

	//slots
	n = rnd( n );
	int slot_roll = 0;
	if( n & 128 )
	{
		n = rnd( n );
		slot_roll = n % 100;
	}
	else
	{
		m = rnd( m );
		slot_roll = m % 100;
	}

	const unsigned num_slots = GetNumSlots( charm_type, slot_table, slot_roll );

	return num_slots >= charm->num_slots;
}

bool GenerateCharm3( const unsigned charm_type, const unsigned table, int n, int m, Charm^ charm )
{
	//check charm_type < StaticData::skill1_table->Length
	array< unsigned char >^ skill1_table = StaticData::skill1_table[ charm_type ];
	const unsigned num_skills1 = skill1_table->Length / 3;
	//check charm_type < StaticData::skill2_table->Length
	array< signed char >^ skill2_table = StaticData::skill2_table[ charm_type ];
	const unsigned num_skills2 = skill2_table == nullptr ? 0 : skill2_table->Length / 3;

	//skill 1
	//n = rnd( n );
	const int skill1_index = n % num_skills1;
	const int skill1_name = skill1_table[ skill1_index * 3 ];
	const int skill1_min = skill1_table[ skill1_index * 3 + 1 ];
	const int skill1_max = skill1_table[ skill1_index * 3 + 2 ];

	//skill 1 point
	n = rnd( n );
	//m = rnd( m );
	const int point1 = skill1_min + (n^m) % ( skill1_max - skill1_min + 1 );

	//has skill 2?
	int skill2_index = -1, point2 = 0, skill2_min = 0, skill2_max = 0, skill2_name = 0;
	n = rnd( n );
	m = rnd( m );
	if( (n^m) % 100 >= StaticData::skill2_chance_table[ charm_type ] )
	{
		//skill 2
		m = rnd( m );
		skill2_index = m % num_skills2;
		skill2_name = skill2_table[ skill2_index * 3 ];
		skill2_min = skill2_table[ skill2_index * 3 + 1 ];
		skill2_max = skill2_table[ skill2_index * 3 + 2 ];

		//skill 2 point
		if( skill2_min > 0 ) //positive number
		{
			n = rnd( n );
			m = rnd( m );
			point2 = ( (n^m) % ( skill2_max - skill2_min + 1 ) ) + skill2_min;
		}
		else //check for negative
		{
			n = rnd( n );
			if( n % 2 == 1 ) //positive
			{
				n = rnd( n );
				m = rnd( m );
				point2 = (n^m) % skill2_max + 1;
			}
			else //negative
			{
				n = rnd( n );
				m = rnd( m );
				point2 = skill2_min + (n^m) % ( -skill2_min );
			}
		}		

		if( skill1_name == skill2_name )
		{
			skill2_min = skill2_max = point2 = 0;
			skill2_name = skill2_index = Ability::static_abilities.Count;
		}
	}

	const int slot_table = (int)Math::Floor( point1*10.0 / skill1_max + ( (point2 > 0) ? point2*10.0 / skill2_max : 0 ) );

	//slots
	n = rnd( n );
	int slot_roll = 0;
	if( n & 128 )
	{
		n = rnd( n );
		slot_roll = n % 100;
	}
	else
	{
		m = rnd( m );
		slot_roll = m % 100;
	}

	const int num_slots = GetNumSlots( charm_type, slot_table, slot_roll );

	if( num_slots != charm->num_slots )
		return false;

	if( point2 == 0 )
	{
		if( charm->abilities.Count == 2 ||
			skill1_name != charm->abilities[ 0 ]->ability->static_index ||
			point1 != charm->abilities[ 0 ]->amount )
			return false;
	}
	else
	{
		if( charm->abilities.Count != 2 ||
			skill1_name != charm->abilities[ 0 ]->ability->static_index ||
			skill2_name != charm->abilities[ 1 ]->ability->static_index ||
			point1 != charm->abilities[ 0 ]->amount ||
			point2 != charm->abilities[ 1 ]->amount )
			return false;
	}
	return true;
}

array< unsigned char >^ LoadSlotTable( String^ filename )
{
	array< String^ >^ lines = IO::File::ReadAllLines( filename );
	array< unsigned char >^ result = gcnew array< unsigned char >( lines->Length * 3 - 3 );
	for( int i = 1, index = 0; i < lines->Length; ++i )
	{
		array< String^ >^ tokens = lines[ i ]->Split( ',' );
		for( int j = 1; j < tokens->Length; ++j )
			result[ index++ ] = (unsigned char)Convert::ToUInt16( tokens[ j ] );
	}
	return result;
}

template< typename T >
array< T >^ LoadSkillTable( String^ filename )
{
	array< String^ >^ lines = IO::File::ReadAllLines( filename );
	array< T >^ result = gcnew array< T >( lines->Length * 3 - 3 );
	for( int i = 1, index = 0; i < lines->Length; ++i )
	{
		array< String^ >^ tokens = lines[ i ]->Split( ',' );
		
		Ability^ ab = Ability::FindCharmAbility( tokens[ 0 ] );
		result[ index++ ] = ab->static_index;
		result[ index++ ] = (T)Convert::ToInt16( tokens[ 1 ] );
		result[ index++ ] = (T)Convert::ToInt16( tokens[ 2 ] );
	}
	return result;
}

void LoadCharmTableData()
{
	array< String^ >^ charm_names =
	{
		"mystery",
		"shining",
		"ancient",
		"enduring",
	};

	const unsigned NumCharmTypes = charm_names->Length;

	StaticData::skill1_table = gcnew array< array< unsigned char >^ >( NumCharmTypes );
	StaticData::skill2_table = gcnew array< array< signed char >^ >( NumCharmTypes );
	StaticData::slot_table = gcnew array< array< unsigned char >^ >( NumCharmTypes );

	for( unsigned i = 0; i < NumCharmTypes; ++i )
	{
		StaticData::slot_table[ i ] = LoadSlotTable( "Data/Charm Generation/" + charm_names[ i ] + "_slots.csv" );
		StaticData::skill1_table[ i ] = LoadSkillTable< unsigned char >( "Data/Charm Generation/" + charm_names[ i ] + "_skill1.csv" );
		if( i > 0 )
			StaticData::skill2_table[ i ] = LoadSkillTable< signed char >( "Data/Charm Generation/" + charm_names[ i ] + "_skill2.csv" );
	}
}

unsigned char GetMaxSingleSkill( const int index, const unsigned charm_type )
{
	unsigned char res = 0;
	for( int i = 0; i < StaticData::skill1_table[ charm_type ]->Length; i += 3 )
	{
		if( StaticData::skill1_table[ charm_type ][ i ] == index )
			res = Math::Max( res, StaticData::skill1_table[ charm_type ][ i + 2 ] );
	}
	if( StaticData::skill2_table[ charm_type ] )
	{
		for( int i = 0; i < StaticData::skill2_table[ charm_type ]->Length; i += 3 )
		{
			if( (unsigned char)StaticData::skill2_table[ charm_type ][ i ] == index )
				res = Math::Max( res, (unsigned char)StaticData::skill2_table[ charm_type ][ i + 2 ] );
		}
	}
	return res;
}

void SetupSingleSkillMaxs()
{
	for( int i = 0; i < Ability::static_abilities.Count; ++i )
	{
		Ability^ a = Ability::static_abilities[ i ];
		a->max_vals1 = gcnew array< unsigned char >( CharmDatabase::NumCharmTypes );
		for( unsigned charm_type = 0; charm_type < CharmDatabase::NumCharmTypes; charm_type++ )
		{
			a->max_vals1[ charm_type ]= GetMaxSingleSkill( a->static_index, charm_type );
		}
	}
}

void CreateTableSeedList()
{
	CharmDatabase::table_seed_list = gcnew array< List_t< unsigned short >^ >( CharmDatabase::table_seeds->Length );
	for( int i = 0; i < CharmDatabase::table_seeds->Length; ++i )
	{
		CharmDatabase::table_seed_list[ i ] = gcnew List_t< unsigned short >();

		int n = CharmDatabase::table_seeds[ i ];
		do 
		{
			CharmDatabase::table_seed_list[ i ]->Add( (unsigned short)(n & 0xFFFF) );
			n = rnd( n );
		}
		while( n != CharmDatabase::table_seeds[ i ] );

		CharmDatabase::table_seed_list[ i ]->Sort();
	}
}

int FindTable( const int n )
{
	for( int i = 0; i < CharmDatabase::table_seed_list->Length; ++i )
	{
		if( CharmDatabase::table_seed_list[i]->BinarySearch( n ) >= 0 )
			return i;
	}
	return -1;
}

void CharmDatabase::GenerateCharmTable()
{
	location_cache = gcnew Map_t< System::String^, CharmLocationDatum^ >();
	LoadCharmTableData();
	CreateTableSeedList();
	SetupSingleSkillMaxs();

	return;
}

#pragma endregion

bool CanFind2SkillCharm( Charm^ charm )
{
	if( charm->abilities.Count < 2 )
		return false;

	const unsigned start = ( charm->num_slots == 3 ) ? 2 : 1;

	array< int >^ skill1_index = gcnew array< int >( CharmDatabase::NumCharmTypes );
	array< int >^ skill2_index = gcnew array< int >( CharmDatabase::NumCharmTypes );

	//quick check first
	for( unsigned charm_type = start; charm_type < CharmDatabase::NumCharmTypes; ++charm_type )
	{
		skill1_index[ charm_type ] = -1;
		skill2_index[ charm_type ] = -1;

		array< unsigned char >^ t1 = StaticData::skill1_table[ charm_type ];
		bool t1_tight = false;
		for( int i = 0; i < t1->Length; i += 3 )
		{
			if( t1[i] == charm->abilities[0]->ability->static_index )
			{
				if( charm->abilities[0]->amount <= t1[i+2] )
					skill1_index[ charm_type ] = i;
				t1_tight = charm->abilities[0]->amount == t1[i+2];
				break;
			}
		}
		if( skill1_index[ charm_type ] == -1 )
			continue;

		array< signed char >^ t2 = StaticData::skill2_table[ charm_type ];
		bool t2_tight = false;
		for( int i = 0; i < t2->Length; i += 3 )
		{
			if( (unsigned char)t2[i] == charm->abilities[1]->ability->static_index )
			{
				if( charm->abilities[1]->amount <= t2[i+2] )
					skill2_index[ charm_type ] = i;
				t2_tight = charm->abilities[1]->amount == t2[i+2];
				break;
			}
		}
		if( skill2_index[ charm_type ] == -1 )
			continue;

		if( charm->num_slots == 0 )
			return true;

		if( !t1_tight && !t2_tight )
			return true;
	}

	//slow check if needed

	for( unsigned charm_type = start; charm_type < CharmDatabase::NumCharmTypes; ++charm_type )
	{
		if( skill1_index[ charm_type ] < 0 ||
			skill2_index[ charm_type ] < 0 )
			continue;

		const unsigned skill1_table_index = skill1_index[ charm_type ];
		const unsigned skill2_table_index = skill2_index[ charm_type ];
		
		const unsigned num1 = StaticData::skill1_table[ charm_type ]->Length / 3;
		const unsigned num2 = StaticData::skill2_table[ charm_type ]->Length / 3;

		for( int n = skill1_table_index / 3; n < 65363; n += num1 )
		{
			for( int m = skill2_table_index / 3; m < 65363; m += num2 )
			{
				const int initm = reverse_rnd(reverse_rnd(m == 0 ? num2 : m));
				if( CanGenerate2SkillCharm( charm_type, n, initm, charm ) )
					return true;
			}
		}
	}

	return false;
}

bool CharmDatabase::CharmIsLegal( Charm^ charm )
{
	if( charm->num_slots >= 4 )
		return false;

	if( charm->abilities.Count == 0 )
		return true;
	else if( charm->abilities.Count == 1 )
	{
		AbilityPair^ ap = charm->abilities[0];
		const unsigned start[4] =
		{
			0,
			0,
			1,
			2
		};
		for( unsigned c = start[ charm->num_slots ]; c < CharmDatabase::NumCharmTypes; ++c )
			if( ap->amount <= ap->ability->max_vals1[c] )
				return true;
	}
	else if( charm->abilities.Count == 2 )
	{
		if( !CanFind2SkillCharm( charm ) )
		{
			AbilityPair^ temp = charm->abilities[0];
			charm->abilities[0] = charm->abilities[1];
			charm->abilities[1] = temp;
			
			const bool res = CanFind2SkillCharm( charm );
			
			temp = charm->abilities[0];
			charm->abilities[0] = charm->abilities[1];
			charm->abilities[1] = temp;
			
			return res;
		}
		else return true;
	}
	return false;
}

void FindTwoSkillCharms( array< List_t< unsigned >^ >^ charms, const int n0, const int m0, const int num1, const int num2, const unsigned t )
{
	for( int n = n0; n < 65363; n += num1 )
	{
		for( int m = m0; m < 65363; m += num2 )
		{
			const int initm = reverse_rnd(reverse_rnd(m == 0 ? num2 : m));
			if( TryTwoSkillCharm( t, n, initm, charms ) )
				return;
		}
	}
}

void FindTwoSkillCharms( array< List_t< unsigned >^ >^ charms, const unsigned index1, const unsigned index2, const unsigned max_charm_type )
{
	for( unsigned charm_type = 1; charm_type <= max_charm_type; ++charm_type )
	{
		int skill1_table_index;
		for( skill1_table_index = 0; skill1_table_index < StaticData::skill1_table[ charm_type ]->Length; skill1_table_index += 3 )
		{
			if( StaticData::skill1_table[ charm_type ][ skill1_table_index ] == index1 )
				break;
		}
		if( skill1_table_index >= StaticData::skill1_table[ charm_type ]->Length )
			continue;

		int skill2_table_index = -1;
		for( skill2_table_index = 0; skill2_table_index < StaticData::skill2_table[ charm_type ]->Length; skill2_table_index += 3 )
		{
			if( StaticData::skill2_table[ charm_type ][ skill2_table_index ] == index2 )
				break;
		}
		if( skill2_table_index >= StaticData::skill2_table[ charm_type ]->Length )
			continue;

		const unsigned num_skills1 = StaticData::skill1_table[ charm_type ]->Length / 3;
		const unsigned num_skills2 = StaticData::skill2_table[ charm_type ]->Length / 3;

		FindTwoSkillCharms( charms, skill1_table_index / 3, skill2_table_index / 3, num_skills1, num_skills2, charm_type );
	}
}

bool ContainsBetterCharm( List_t< Charm^ >^ charms, const int p1, const int p2, Ability^ ab1, Ability^ ab2 )
{
	for( int i = 0; i < charms->Count; ++i )
	{
		if( charms[i]->abilities[0]->ability == ab1 &&
			charms[i]->abilities[1]->ability == ab2 &&
			charms[i]->abilities[0]->amount >= p1 &&
			charms[i]->abilities[1]->amount >= p2 )
			return true;
	}
	return false;
}

bool ContainsBetterCharm( List_t< Charm^ >^ charms, Charm^ charm )
{
	for each( Charm^ c in charms )
	{
		if( c->abilities[0]->ability == charm->abilities[0]->ability &&
			c->abilities[0]->amount >= charm->abilities[0]->amount )
			return true;
		if( c->abilities.Count == 2 &&
			c->abilities[1]->ability == charm->abilities[0]->ability &&
			c->abilities[1]->amount >= charm->abilities[0]->amount )
			return true;
	}
	return false;
}

void GetDoubleSkillCharms( List_t< Charm^ >^ list, List_t< Skill^ >% skills, const unsigned max_charm_type )
{
	const unsigned max_slots = Math::Min( 3u, max_charm_type + 1 );
	
	array< List_t< Charm^ >^ >^ two_skills = gcnew array< List_t< Charm^ >^ >( max_slots + 1 );
	for( unsigned k = 0; k <= max_slots; ++k )
		two_skills[ k ] = gcnew List_t< Charm^ >();

	for( int i = 1; i < skills.Count; ++i )
	{
		Ability^ ab1 = skills[ i ]->ability;
		for( int j = 0; j < i; ++j )
		{
			Ability^ ab2 = skills[ j ]->ability;

			array< List_t< unsigned >^ >^ charms = gcnew array< List_t< unsigned >^ >( max_slots + 1 );
			for( unsigned k = 0; k <= max_slots; ++k )
				charms[ k ] = gcnew List_t< unsigned >();

			FindTwoSkillCharms( charms, ab1->static_index, ab2->static_index, max_charm_type );

			for( unsigned k = 0; k <= max_slots; ++k )
			{
				for( unsigned l = charms[k]->Count; l --> 0; )
				{
					const int p1 = charms[k][l] >> 16;
					const int p2 = charms[k][l] & 0xFFFF;
					if( !ContainsBetterCharm( two_skills[k], p1, p2, ab1, ab2 ) )
					{
						Charm^ c = gcnew Charm( k );
						c->abilities.Add( gcnew AbilityPair( ab1, p1 ) );
						c->abilities.Add( gcnew AbilityPair( ab2, p2 ) );

						two_skills[k]->Add( c );
					}
				}
			}

			for( unsigned k = 0; k <= max_slots; ++k )
				charms[ k ]->Clear();

			FindTwoSkillCharms( charms, ab2->static_index, ab1->static_index, max_charm_type );

			for( unsigned k = 0; k <= max_slots; ++k )
			{
				for( unsigned l = charms[k]->Count; l --> 0; )
				{
					const int p1 = charms[k][l] >> 16;
					const int p2 = charms[k][l] & 0xFFFF;
					if( !ContainsBetterCharm( two_skills[k], p1, p2, ab2, ab1 ) )
					{
						Charm^ c = gcnew Charm( k );
						c->abilities.Add( gcnew AbilityPair( ab2, p1 ) );
						c->abilities.Add( gcnew AbilityPair( ab1, p2 ) );

						two_skills[k]->Add( c );
					}
				}
			}
		}
	}

	for each( Charm^ c in list )
	{
		if( !ContainsBetterCharm( two_skills[ c->num_slots ], c ) )
			two_skills[ c->num_slots ]->Add( c );
	}

	list->Clear();
	for( unsigned k = 0; k <= max_slots; ++k )
		list->AddRange( two_skills[ k ] );
}

void GetSingleSkillCharms( List_t< Charm^ >^ list, List_t< Skill^ >% skills, const unsigned max_charm_type )
{
	const unsigned max_slots = Math::Min( 3u, max_charm_type + 1 );
	for each( Skill^ skill in skills )
	{
		if( skill->ability->max_vals1 == nullptr )
			continue;

		for( unsigned num_slots = 1; num_slots <= max_slots; ++num_slots )
		{
			unsigned max_val = 0;
			for( unsigned s = num_slots - 1; s <= max_charm_type; ++s )
				max_val = Math::Max( max_val, (unsigned)skill->ability->max_vals1[ s ] );

			if( max_val > 0 )
			{
				Charm^ ct = gcnew Charm( num_slots );
				ct->abilities.Add( gcnew AbilityPair( skill->ability, max_val ) );
				list->Add( ct );
			}
		}
	}
}

void AddSlotOnlyCharms( List_t< Charm^ >^ res, Query^ query, const unsigned max_charm_type )
{
	bool have[ 4 ] = { false, false, false, false };
	for each( Charm^ charm in res )
	{
		have[ charm->num_slots ] = true;
	}

	for( unsigned slots = Math::Min( 3u, max_charm_type + 1 ); slots > 0; --slots )
	{
		if( !have[ slots ] )
		{
			res->Add( gcnew Charm( slots ) );
			break;
		}
	}
}

List_t< Charm^ >^ CharmDatabase::GetCharms( Query^ query, const bool use_two_skill_charms )
{
	List_t< Charm^ >^ res = gcnew List_t< Charm^ >;
	const unsigned max_charm_type = CalcMaxCharmType( query );

	GetSingleSkillCharms( res, query->skills, max_charm_type );

	if( use_two_skill_charms && max_charm_type > 0 )
		GetDoubleSkillCharms( res, query->skills, max_charm_type );

	AddSlotOnlyCharms( res, query, max_charm_type );

	return res;
}

CharmLocationDatum^ CharmDatabase::FindCharmLocations( Charm^ charm )
{
	CharmLocationDatum^ result = gcnew CharmLocationDatum();
	result->table = gcnew array< unsigned, 2 >( 17, NumCharmTypes );
	result->examples = gcnew array< System::String^ >( 17 );
	const unsigned limit = 64000;

	unsigned num_found = 0;
	for( unsigned charm_type = 0; charm_type < NumCharmTypes; ++charm_type )
	{
		if( charm->num_slots > charm_type + 1 )
			continue;

		if( charm->abilities.Count == 0 )
		{
			for( int t = 0; t < table_seeds->Length; ++t )
				result->table[t, charm_type] = limit;
			continue;
		}

		int skill1_table_index = -1;
		for( skill1_table_index = 0; skill1_table_index < StaticData::skill1_table[ charm_type ]->Length; skill1_table_index += 3 )
		{
			if( StaticData::skill1_table[ charm_type ][ skill1_table_index ] == charm->abilities[0]->ability->static_index &&
				StaticData::skill1_table[ charm_type ][ skill1_table_index + 2 ] >= charm->abilities[0]->amount )
				break;
		}

		const unsigned num1 = StaticData::skill1_table[ charm_type ]->Length / 3;

		if( charm->abilities.Count == 1 )
		{
			int skill2_table_index = -1;
			if( StaticData::skill2_table[ charm_type ] )
			{
				for( skill2_table_index = 0; skill2_table_index < StaticData::skill2_table[ charm_type ]->Length; skill2_table_index += 3 )
				{
					if( StaticData::skill2_table[ charm_type ][ skill2_table_index ] == charm->abilities[0]->ability->static_index &&
						StaticData::skill2_table[ charm_type ][ skill2_table_index + 2 ] >= charm->abilities[0]->amount )
						break;
				}
			}
			if( skill1_table_index >= StaticData::skill1_table[ charm_type ]->Length &&
				( skill2_table_index < 0 || skill2_table_index >= StaticData::skill2_table[ charm_type ]->Length ) )
				continue;

			for( int t = 0; t < table_seeds->Length; ++t )
			{
				if( t == 10 || t == 11  || t == 14 || t == 15 || t == 16 )
				{
					//do nothing. see MH4 implementation
				}
				else
				{
					result->table[t, charm_type] = limit;
				}
			}
		}
		else if( StaticData::skill2_table[ charm_type ] )
		{
			if( skill1_table_index >= StaticData::skill1_table[ charm_type ]->Length )
				continue;

			int skill2_table_index = -1;
			for( skill2_table_index = 0; skill2_table_index < StaticData::skill2_table[ charm_type ]->Length; skill2_table_index += 3 )
			{
				if( StaticData::skill2_table[ charm_type ][ skill2_table_index ] == charm->abilities[1]->ability->static_index &&
					StaticData::skill2_table[ charm_type ][ skill2_table_index + 2 ] >= charm->abilities[1]->amount )
					break;
			}
			if( skill2_table_index >= StaticData::skill2_table[ charm_type ]->Length )
				continue;

			const unsigned num2 = StaticData::skill2_table[ charm_type ]->Length / 3;

			for( int n = skill1_table_index / 3; n < 65363; n += num1 )
			{
				const int table = FindTable( n );
				if( table == -1 )
					continue;

				for( int m = skill2_table_index / 3; m < 65363; m += num2 )
				{
					const int initm = reverse_rnd(reverse_rnd(m == 0 ? num2 : m));
					String^ str = CanGenerateCharm2( charm_type, n, initm, charm );
					if( str )
					{
						if( result->table[table, charm_type]++ == 0 )
							result->examples[ table ] = str;
						else
							result->examples[ table ] = nullptr;

						if( num_found++ == 0 )
							result->example = str;
						else result->example = nullptr;
					}
				}
			}
		}
	}
	return result;
}
