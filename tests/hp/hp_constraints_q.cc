//----------------------------  hp_constraints_q.cc  ---------------------------
//    $Id$
//    Version: $Name$ 
//
//    Copyright (C) 2006 by the deal.II authors
//
//    This file is subject to QPL and may not be  distributed
//    without copyright and license information. Please refer
//    to the file deal.II/doc/license.html for the  text  and
//    further information on this license.
//
//----------------------------  hp_constraints_q.cc  ---------------------------


// check that computation of hp constraints works for Q elements correctly

char logname[] = "hp_constraints_q/output";


#include "hp_constraints_common.h"


template <int dim>
void test ()
{
  deallog << "Test for dim = " << dim << std::endl << std::endl;
  hp::FECollection<dim> fe;
  std::vector<unsigned int> degrees;
  for (unsigned int i=1; i<4; ++i)
    {
      fe.push_back (FE_Q<dim>(i));
      degrees.push_back (i);
    }

  deallog << "No hanging nodes test" << std::endl;
  test_no_hanging_nodes  (fe);
  deallog << std::endl << std::endl;

  deallog << "Hanging nodes test" << std::endl;
  test_with_hanging_nodes  (fe);
  deallog << std::endl << std::endl;

  deallog << "Wrong face orientation test" << std::endl;
  test_with_wrong_face_orientation  (fe);
  deallog << std::endl << std::endl;

  deallog << "2d deformed mesh test" << std::endl;
  test_with_2d_deformed_mesh  (fe);
  deallog << std::endl << std::endl;

  deallog << "2d deformed refined mesh test" << std::endl;
  test_with_2d_deformed_refined_mesh  (fe);
  deallog << std::endl << std::endl;

  deallog << "Interpolation test" << std::endl;
  test_interpolation  (fe, degrees);
  deallog << std::endl << std::endl;

  deallog << "Random hanging nodes" << std::endl;
  test_with_hanging_nodes_random (fe);

  deallog << std::endl << std::endl;
  deallog << std::endl << std::endl;
}
