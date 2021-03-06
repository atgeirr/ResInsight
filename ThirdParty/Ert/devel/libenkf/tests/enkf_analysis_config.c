/*
   Copyright (C) 2013  Statoil ASA, Norway.

   The file 'enkf_analysis_config.c' is part of ERT - Ensemble based Reservoir Tool.

   ERT is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   ERT is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.

   See the GNU General Public License at <http://www.gnu.org/licenses/gpl.html>
   for more details.
*/
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <ert/util/test_util.h>
#include <ert/util/test_work_area.h>
#include <ert/util/util.h>
#include <ert/util/rng.h>

#include <ert/config/config_parser.h>
#include <ert/config/config_content.h>

#include <ert/enkf/analysis_config.h>
#include <ert/enkf/config_keys.h>


analysis_config_type * create_analysis_config() {
  rng_type * rng = rng_alloc( MZRAN , INIT_DEFAULT );
  analysis_config_type * ac = analysis_config_alloc( rng );
  return ac;
}


void test_create() {
  analysis_config_type * ac = create_analysis_config( );
  test_assert_true( analysis_config_is_instance( ac ) );
  analysis_config_free( ac );
}


void test_min_realizations_percent(const char * num_realizations_str, const char * min_realizations_str, int min_realizations){
  test_work_area_type * work_area = test_work_area_alloc("test_min_realizations");

  {
    FILE * config_file_stream = util_mkdir_fopen("config_file", "w");
    test_assert_not_NULL(config_file_stream);
    fprintf(config_file_stream, num_realizations_str);
    fprintf(config_file_stream, min_realizations_str);
    fclose(config_file_stream);

    config_parser_type * c = config_alloc();
    config_schema_item_type * item = config_add_schema_item(c , NUM_REALIZATIONS_KEY , true );
    config_schema_item_set_default_type(item, CONFIG_INT);
    config_schema_item_set_argc_minmax( item , 1 , 1);
    item = config_add_schema_item(c , MIN_REALIZATIONS_KEY , false );
    config_schema_item_set_argc_minmax( item , 1 , 2);
    {
      config_content_type * content = config_parse(c , "config_file" , "--" , NULL , NULL , false , true );
      test_assert_true(config_content_is_valid(content));

      analysis_config_type * ac = create_analysis_config( );
      analysis_config_init(ac, content);

      test_assert_int_equal( min_realizations , analysis_config_get_min_realisations( ac ) );

      analysis_config_free( ac );
      config_content_free( content );
      config_free( c );
    }
  }

  test_work_area_free(work_area);
}



void test_min_realisations( ) {
  analysis_config_type * ac = create_analysis_config( );
  test_assert_int_equal( 0 , analysis_config_get_min_realisations( ac ) );
  analysis_config_set_min_realisations( ac , 26 );
  test_assert_int_equal( 26 , analysis_config_get_min_realisations( ac ) );
  analysis_config_free( ac );
}


void test_continue( ) {
  analysis_config_type * ac = create_analysis_config( );

  test_assert_true( analysis_config_have_enough_realisations( ac , 10 ));
  test_assert_false( analysis_config_have_enough_realisations( ac , 0 ));

  analysis_config_set_min_realisations( ac , 5 );
  test_assert_true( analysis_config_have_enough_realisations( ac , 10 ));

  analysis_config_set_min_realisations( ac , 15 );
  test_assert_false( analysis_config_have_enough_realisations( ac , 10 ));

  analysis_config_set_min_realisations( ac , 10 );
  test_assert_true( analysis_config_have_enough_realisations( ac , 10 ));

  analysis_config_set_min_realisations( ac , 0 );
  test_assert_true( analysis_config_have_enough_realisations( ac , 10 ));

  analysis_config_free( ac );
}


void test_current_module_options() {
  analysis_config_type * ac = create_analysis_config( );
  test_assert_NULL( analysis_config_get_active_module( ac ));
  analysis_config_load_internal_module(ac , "STD_ENKF" , "std_enkf_symbol_table");

  test_assert_false( analysis_config_get_module_option( ac , ANALYSIS_SCALE_DATA));
  test_assert_true(analysis_config_select_module(ac , "STD_ENKF"));
  test_assert_false( analysis_config_select_module(ac , "DOES_NOT_EXIST"));

  test_assert_true( analysis_module_is_instance( analysis_config_get_active_module( ac )));
  test_assert_true( analysis_config_get_module_option( ac , ANALYSIS_SCALE_DATA));
  test_assert_false( analysis_config_get_module_option( ac , ANALYSIS_ITERABLE));
  analysis_config_free( ac );
}

void test_stop_long_running( ) {
  analysis_config_type * ac = create_analysis_config( );
  test_assert_bool_equal( false , analysis_config_get_stop_long_running( ac ) );
  analysis_config_set_stop_long_running( ac , true );
  test_assert_bool_equal( true , analysis_config_get_stop_long_running( ac ) );
  analysis_config_free( ac );
}

int main(int argc , char ** argv) {
  test_create();
  test_min_realisations();
  test_continue();

  {
    const char * num_realizations_str = "NUM_REALIZATIONS 80\n";
    const char * min_realizations_str = "MIN_REALIZATIONS 10%%\n";
    int min_realizations              = 8;
    test_min_realizations_percent(num_realizations_str, min_realizations_str, min_realizations);
  }
  {
    const char * num_realizations_str = "NUM_REALIZATIONS 8\n";
    const char * min_realizations_str = "MIN_REALIZATIONS 50%%\n";
    int min_realizations              = 4;
    test_min_realizations_percent(num_realizations_str, min_realizations_str, min_realizations);
  }
  {
    const char * num_realizations_str = "NUM_REALIZATIONS 80\n";
    const char * min_realizations_str = "MIN_REALIZATIONS 2\n";
    int min_realizations              = 2;
    test_min_realizations_percent(num_realizations_str, min_realizations_str, min_realizations);
  }
  {
    const char * num_realizations_str = "NUM_REALIZATIONS 8\n";
    const char * min_realizations_str = "MIN_REALIZATIONS 10%%\n";
    int min_realizations              = 0;
    test_min_realizations_percent(num_realizations_str, min_realizations_str, min_realizations);
  }
  {
    const char * num_realizations_str = "NUM_REALIZATIONS 900\n";
    const char * min_realizations_str = "MIN_REALIZATIONS 10 \n";
    int min_realizations              = 10;
    test_min_realizations_percent(num_realizations_str, min_realizations_str, min_realizations);
  }
  {
    const char * num_realizations_str = "NUM_REALIZATIONS 900\n";
    int min_realizations              = 0;
    test_min_realizations_percent(num_realizations_str, "", min_realizations);
  }

  test_current_module_options();
  test_stop_long_running();
  exit(0);
}

