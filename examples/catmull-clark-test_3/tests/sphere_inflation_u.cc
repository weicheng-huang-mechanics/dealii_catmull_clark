/* ---------------------------------------------------------------------
 *
 * Copyright (C) 1999 - 2019 by the deal.II authors
 *
 * This file is part of the deal.II library.
 *
 * The deal.II library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE.md at
 * the top level directory of deal.II.
 *
 * ---------------------------------------------------------------------
 */
#include <deal.II/grid/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/manifold_lib.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/grid_out.h>
#include <deal.II/grid/grid_in.h>
#include <deal.II/grid/grid_reordering.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/mapping_fe_field.h>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>
#include <deal.II/base/numbers.h>
#include <deal.II/base/index_set.h>
#include <deal.II/base/quadrature_point_data.h>

#include <deal.II/lac/vector.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/sparse_matrix.h>
#include <deal.II/lac/sparse_direct.h>

#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/block_sparsity_pattern.h>
#include <deal.II/lac/arpack_solver.h>

#include <deal.II/lac/linear_operator.h>
#include <deal.II/lac/packaged_operation.h>
#include <deal.II/lac/constrained_linear_operator.h>

#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/precondition.h>

#include <deal.II/hp/dof_handler.h>
#include <deal.II/hp/fe_values.h>

#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>

#include <fstream>
#include <iostream>
#include <complex>

#include "Catmull_Clark_Data.hpp"
#include "polynomials_Catmull_Clark.hpp"
#include "FE_Catmull_Clark.hpp"
#include "MappingFEField_hp.hpp"

#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPoints.h>
#include <vtkQuad.h>
#include <vtkPointData.h>
#include <vtkDoubleArray.h>
#include <vtkXMLUnstructuredGridWriter.h>

// The final step, as in previous programs, is to import all the deal.II class
// and function names into the global namespace:
using namespace dealii;

Tensor<2, 2> metric_covariant(const Tensor<2, 3> a_cov)
{
    Tensor<2, 2> am_cov;
    for (unsigned int ii=0; ii<2; ++ii)
    {
        for (unsigned int jj=0; jj<2 ; ++jj)
        {
            am_cov[ii][jj] =scalar_product(a_cov[ii], a_cov[jj]);
        }
    }
    return am_cov;
}



Tensor<2, 2> metric_contravariant(const Tensor<2, 2> am_cov)
{
    return transpose(invert(am_cov));
}



template <int spacedim>
Point<spacedim> grid_y_transform (const Point<spacedim> &pt_in)
{
    const double &x = pt_in[0];
    const double &y = pt_in[1];
    const double y_upper = 44.0 + (16.0/48.0)*x; // Line defining upper edge of beam
    const double y_lower =  0.0 + (44.0/48.0)*x; // Line defining lower edge of beam
    const double theta = y/44.0; // Fraction of height along left side of beam
    const double y_transform = (1-theta)*y_lower + theta*y_upper; // Final transformation
    Point<spacedim> pt_out = pt_in;
    pt_out[1] = y_transform;
    return pt_out;
}



template <int dim, int spacedim>
void cooks_membrane(Triangulation<dim, spacedim> &triangulation)
{
    std::vector< unsigned int > repetitions(dim, 10);
    repetitions[1] = 4;
    const Point<dim> bottom_left =  Point<dim>(0.0, 0.0) ;
    const Point<dim> top_right = Point<dim>(48.0, 44.0);
    GridGenerator::subdivided_hyper_rectangle(triangulation, repetitions, bottom_left, top_right);
    GridTools::transform(&grid_y_transform<spacedim>, triangulation);
}



void vtk_plot(const std::string &filename, const hp::DoFHandler<2, 3> &dof_handler, const hp::MappingCollection<2, 3> mapping, const Vector<double> vertices, const Vector<double> solution, const Vector<double> potential = Vector<double>()){
    
    //    auto verts = dof_handler.get_triangulation().get_vertices();
    
    const unsigned int ngridpts = 10;
    const unsigned int seg_n = ngridpts-1;
    vtkSmartPointer<vtkUnstructuredGrid> grid = vtkUnstructuredGrid::New();
    vtkSmartPointer<vtkPoints> points = vtkPoints::New();
    vtkSmartPointer<vtkDoubleArray> function = vtkDoubleArray::New();
    vtkSmartPointer<vtkDoubleArray> function_2 = vtkDoubleArray::New();
    vtkSmartPointer<vtkDoubleArray> normal = vtkDoubleArray::New();
    vtkSmartPointer<vtkDoubleArray> stretch = vtkDoubleArray::New();
    
    function->SetNumberOfComponents(3);
    function->SetName("disp");
    function->SetComponentName(0, "x");
    function->SetComponentName(1, "y");
    function->SetComponentName(2, "z");
    
    normal->SetNumberOfComponents(3);
    normal->SetName("normal");
    normal->SetComponentName(0, "x");
    normal->SetComponentName(1, "y");
    normal->SetComponentName(2, "z");
    
    stretch->SetNumberOfComponents(1);
    stretch->SetName("stretch");
    stretch->SetComponentName(0, "value");
    
    if (potential.size() != 0){
        function_2->SetNumberOfComponents(3);
        function_2->SetName("pressure");
        function_2->SetComponentName(0, "value1");
        function_2->SetComponentName(1, "value2");
        function_2->SetComponentName(2, "value3");
    }
    
    int sample_offset = 0;
    int count = 0;
    double seg_length = 1./seg_n;
    int numElem = dof_handler.get_triangulation().n_active_cells();
    
    std::vector<types::global_dof_index> local_dof_indices;
    
    for (auto cell=dof_handler.begin_active();cell!=dof_handler.end(); ++cell){
        
        const unsigned int dofs_per_cell = cell->get_fe().dofs_per_cell;
        local_dof_indices.resize(dofs_per_cell);
        cell->get_dof_indices(local_dof_indices);
        for(unsigned int iu = 0; iu < ngridpts; ++iu){
            for(unsigned int iv = 0; iv < ngridpts; ++iv){
                double u = iu*seg_length;
                double v = iv*seg_length;
                //
                Point<3,double> spt = {0,0,0};
                Tensor<1,3,double> disp({0,0,0});
                std::vector<Tensor<1,3>> JJ(3);
                std::vector<Tensor<1,3>> JJ_def(3);
                //                std::vector<Tensor<2,3>> JJ_grad(2);
                double sol = 0;
                for (unsigned int idof = 0; idof < dofs_per_cell; ++idof)
                {
                    double shapes = dof_handler.get_fe(cell->active_fe_index()).shape_value(idof, {u,v});
                    const auto shape_der = dof_handler.get_fe(cell->active_fe_index()).shape_grad(idof, {u,v});
                    
                    sol += shapes * solution[local_dof_indices[idof]];
                    spt[idof % 3] += shapes * vertices[local_dof_indices[idof]];
                    disp[idof % 3] += shapes * solution[local_dof_indices[idof]];
                    
                    JJ[0][idof % 3] += shape_der[0] * vertices[local_dof_indices[idof]];
                    JJ[1][idof % 3] += shape_der[1] * vertices[local_dof_indices[idof]];
                    JJ_def[0][idof % 3] += shape_der[0] * vertices[local_dof_indices[idof]] + shape_der[0] * solution[local_dof_indices[idof]];
                    JJ_def[1][idof % 3] += shape_der[1] * vertices[local_dof_indices[idof]] + shape_der[1] * solution[local_dof_indices[idof]];
                }
                Tensor<1,3,double> p;
                if (potential.size() != 0){
                    for (unsigned int jdof = 0; jdof < dofs_per_cell; ++jdof) {
                        double shapes = dof_handler.get_fe(cell->active_fe_index()).shape_value(jdof, {u,v});
                        p[jdof % 3] += shapes * potential[local_dof_indices[jdof]];
                    }
                }
                
                JJ[2] = cross_product_3d(JJ[0],JJ[1]);
                JJ_def[2] = cross_product_3d(JJ_def[0],JJ_def[1]);
                
                double detJ = JJ[2].norm();
                JJ[2] = JJ[2]/detJ;
                double detJ_def = JJ_def[2].norm();
                JJ_def[2] = JJ_def[2]/detJ_def;
                
                double principle_stretch = sqrt(detJ_def / detJ);
                
                double coordsdata [3] = {spt[0],spt[1],spt[2]};
                
                points->InsertPoint(sample_offset+count, coordsdata);
                
                function->InsertComponent(sample_offset+count, 0, disp[0]);
                function->InsertComponent(sample_offset+count, 1, disp[1]);
                function->InsertComponent(sample_offset+count, 2, disp[2]);
                if (potential.size() != 0)
                {
                    function_2->InsertComponent(sample_offset+count, 0, p[0] / detJ_def * detJ);
                    function_2->InsertComponent(sample_offset+count, 1, p[1] / detJ_def * detJ);
                    function_2->InsertComponent(sample_offset+count, 2, p[2] / detJ_def * detJ);
                }
                normal->InsertComponent(sample_offset+count, 0, JJ[2][0]);
                normal->InsertComponent(sample_offset+count, 1, JJ[2][1]);
                normal->InsertComponent(sample_offset+count, 2, JJ[2][2]);
                
                stretch->InsertComponent(sample_offset+count, 0, principle_stretch);
                
                ++count;
            }
        }
    }
    uint sampleindex = 0;
    //loop over elements
    for(int e = 0; e < numElem; ++e){
        for (unsigned int t = 0 ; t < seg_n; ++t){
            for (unsigned int s = 0; s < seg_n; ++s){
                vtkSmartPointer<vtkCell> cell = vtkQuad::New();
                cell -> GetPointIds() -> SetId(0, sampleindex + t * ngridpts + s);
                cell -> GetPointIds() -> SetId(1, sampleindex + t * ngridpts + s + 1);
                cell -> GetPointIds() -> SetId(2, sampleindex + (t + 1) * ngridpts + s + 1 );
                cell -> GetPointIds() -> SetId(3, sampleindex + (t + 1) * ngridpts + s);
                grid -> InsertNextCell (cell -> GetCellType(), cell -> GetPointIds());
            }
        }
        sampleindex += ngridpts * ngridpts;
    }
    grid -> SetPoints(points);
    grid -> GetPointData() -> AddArray(function);
    if (potential.size() != 0){
        grid -> GetPointData() -> AddArray(function_2);
    }
    grid -> GetPointData() -> AddArray(normal);
    grid -> GetPointData() -> AddArray(stretch);
    vtkSmartPointer<vtkXMLUnstructuredGridWriter> writer = vtkXMLUnstructuredGridWriter::New();
    writer -> SetFileName(filename.c_str());
    writer -> SetInputData(grid);
    if (! writer -> Write()) {
        std::cout<<" Cannot write displacement vtu file! ";
    }
}



template<int dim, int spacedim>
//class material_neo_hookean : public material_class<dim, spacedim>
class material_neo_hookean
{
public:
    material_neo_hookean(const double m,
                         const double h,
                         const Tensor<2, spacedim> a_cov,
                         const Tensor<2, dim, Tensor<1,spacedim>> da_cov)
    :
    mu(m),
    thickness(h),
    a_cov_ref(a_cov),
    da_cov_ref(da_cov),
    am_cov_ref(metric_covariant(a_cov_ref)),
    am_contra_ref ( metric_contravariant(am_cov_ref))
    {
        Tensor<1, spacedim> a3_t = cross_product_3d(a_cov[0], a_cov[1]);
        double a3_bar = a3_t.norm();
        Tensor<1, dim, Tensor<1, spacedim>> a3_t_da;
        for (unsigned int i = 0; i < dim; ++i) {
            a3_t_da[i] = cross_product_3d(da_cov[0][i], a_cov[1]) + cross_product_3d(a_cov[0], da_cov[1][i]);
        }
        Tensor<1, dim> a3_bar_da;
        for (unsigned int i = 0; i < dim; ++i) {
            a3_bar_da[i] = scalar_product(a3_t, a3_t_da[i])/a3_bar;
        }
        for (unsigned int i = 0; i < dim; ++i) {
            da3_ref[i] = a3_t_da[i] / a3_bar -  (( a3_bar_da[i] ) / (a3_bar * a3_bar)) * a3_t;
        }
        
        a_cov_def = a_cov_ref;
        da_cov_def = da_cov_ref;
    }
    
    Tensor<2,dim>  get_stress(const double C_33,
                              const Tensor<2,dim> gm_contra_ref,
                              const Tensor<2,dim> gm_contra_def);
    
    Tensor<4,dim>  get_elastic_tensor(const double C_33,
                                      const Tensor<2,dim> gm_contra_def);
    
    void update(const Tensor<1, dim, Tensor<1,spacedim>> delta_u_der, /* delta u_{,a} */
                const Tensor<2, dim, Tensor<1,spacedim>> delta_u_der2 /* delta u_{,ab} */)
    {
        u_der += delta_u_der;
        u_der2 += delta_u_der2;
        for (unsigned int ia = 0; ia < dim; ++ia){
            a_cov_def[ia] += delta_u_der[ia]; // a_alpha = bar{a_alpha} + u_{,alpha}
            for (unsigned int ib = 0; ib < dim; ++ib){
                da_cov_def[ia][ib] += delta_u_der2[ia][ib]; // a_{alpha,beta} = bar{a_{alpha,beta}} + u_{,alpha beta}
            }
        }
    };
    
    std::pair<std::vector<Tensor<2,dim>>, std::vector<Tensor<4,dim>>> get_integral_tensors();
    
    Tensor<2, spacedim> get_deformed_covariant_bases()
    {
        return a_cov_def;
    }
    
    Tensor<2, dim, Tensor<1,spacedim>> get_deformed_covariant_bases_deriv()
    {
        return da_cov_def;
    }
    
private:
    const double mu;
    // covariant base  a_1, a_2, a_3;
    const Tensor<2, spacedim> a_cov_ref; // a_i = x_{,i} , i = 1,2,3
    // derivatives of covariant base a_1, a_2;
    const Tensor<2, dim, Tensor<1,spacedim>> da_cov_ref; // a_{i,j} = x_{,ij} , i,j = 1,2
    // deformed covariant base  a_1, a_2, a_3;
    Tensor<2, spacedim> a_cov_def; // a_i = x_{,i} , i = 1,2,3
    // deformed derivatives of covariant base a_1, a_2;
    Tensor<2, dim, Tensor<1,spacedim>> da_cov_def; // a_{i,j} = x_{,ij} , i,j = 1,2
    // derivatives of a_3
    Tensor<1, dim, Tensor<1,spacedim>> da3_ref; // a_{3,i}, i = 1,2
    // covariant metric tensor
    const Tensor<2,dim> am_cov_ref;
    // contravariant metric tensor
    const Tensor<2,dim> am_contra_ref;
    // thickness of the shell
    const double thickness;
    
    const QGauss<dim-1> Qh = QGauss<dim-1>(3);
    
    Tensor<1, dim, Tensor<1,spacedim>> u_der;
    
    Tensor<2, dim, Tensor<1,spacedim>> u_der2;
};



template<int dim, int spacedim>
Tensor<2,dim> material_neo_hookean<dim, spacedim> :: get_stress(const double C_33,
                                                                const Tensor<2,dim> gm_contra_ref,
                                                                const Tensor<2,dim> gm_contra_def)
{
    Tensor<2,dim> tau;
    for (unsigned int ia = 0; ia < dim; ++ia)
        for (unsigned int ib = 0; ib < dim; ++ib)
            tau[ia][ib] += mu * (gm_contra_ref[ia][ib] - C_33 * gm_contra_def[ia][ib]);
    
    return tau;
}



template<int dim, int spacedim>
Tensor<4,dim> material_neo_hookean<dim, spacedim> ::get_elastic_tensor(const double C_33,
                                                                       const Tensor<2,dim> gm_contra_def)
{
    Tensor<4,dim> elastic_tensor;
    for (unsigned int ia = 0; ia < dim; ++ia)
        for (unsigned int ib = 0; ib < dim; ++ib)
            for (unsigned int ic = 0; ic < dim; ++ic)
                for (unsigned int id = 0; id < dim; ++id)
                    elastic_tensor[ia][ib][ic][id] += mu  * C_33 * (2 * gm_contra_def[ia][ib] * gm_contra_def[ic][id] + gm_contra_def[ia][ic] * gm_contra_def[ib][id] + gm_contra_def[ia][id] * gm_contra_def[ib][ic] );
    
    return elastic_tensor;
}



template<int dim, int spacedim>
std::pair<std::vector<Tensor<2,dim>>, std::vector<Tensor<4,dim>>>
material_neo_hookean<dim, spacedim> :: get_integral_tensors()
{
    std::vector<Tensor<2,dim>> resultants(2);
    std::vector<Tensor<4,dim>> D_tensors(3);
    for (unsigned int iq_1d = 0; iq_1d < Qh.size(); ++iq_1d) {
        double u_t = Qh.get_points()[iq_1d][0];
        double w_t = Qh.get_weights()[iq_1d];
        double zeta = thickness * (u_t - 0.5);
        Tensor<dim,spacedim> g_cov_ref;
        g_cov_ref[0] = a_cov_ref[0] + zeta * da3_ref[0];
        g_cov_ref[1] = a_cov_ref[1] + zeta * da3_ref[1];
        double J_ratio = cross_product_3d(g_cov_ref[0], g_cov_ref[1]).norm()/cross_product_3d(a_cov_ref[0],a_cov_ref[1]).norm();
        g_cov_ref[2] = a_cov_ref[2]; // Kirchhoff-Love assumption
        
        Tensor<2, dim> gm_cov_ref = metric_covariant(g_cov_ref); // gm_ab
        Tensor<2, dim> gm_contra_ref = metric_contravariant(gm_cov_ref);
        
        Tensor<2, spacedim> g_cov_def = g_cov_ref;
        g_cov_def[0] += u_der[0];
        g_cov_def[1] += u_der[1];
        Tensor<2, dim> gm_cov_def = metric_covariant(g_cov_def);
        Tensor<2, dim> gm_contra_def = metric_contravariant(gm_cov_def);
        
        // for incompressible material
        double g_33 = determinant(gm_cov_ref)/determinant(gm_cov_def); // J_0^{-2}
        
        Tensor<2, dim> stress_tensor = get_stress(g_33, gm_contra_ref, gm_contra_def);
        Tensor<4, dim> elastic_tensor = get_elastic_tensor(g_33, gm_contra_def);
        
        for (unsigned int ia = 0; ia < dim; ++ia) {
            for (unsigned int ib = 0; ib < dim; ++ib) {
                resultants[0][ia][ib] += stress_tensor[ia][ib] * thickness * J_ratio * w_t;
                resultants[1][ia][ib] += stress_tensor[ia][ib] * zeta * thickness * J_ratio * w_t;
                for (unsigned int ic = 0; ic < dim; ++ic) {
                    for (unsigned int id = 0; id < dim; ++id) {
                        D_tensors[0][ia][ib][ic][id] += elastic_tensor[ia][ib][ic][id] * J_ratio * thickness * w_t;
                        D_tensors[1][ia][ib][ic][id] += elastic_tensor[ia][ib][ic][id] * zeta * J_ratio * thickness * w_t;
                        D_tensors[2][ia][ib][ic][id] += elastic_tensor[ia][ib][ic][id] * zeta  * zeta * J_ratio * thickness * w_t;
                    }
                }
            }
        }
    }//loop over thickness quadrature points
    return std::make_pair(resultants, D_tensors);
}



template<int dim, int spacedim>
//class material_mooney_rivlin : public material_class<dim, spacedim>
class material_mooney_rivlin
{
public:
    material_mooney_rivlin(const double c1,
                           const double c2,
                           const double h,
                           const Tensor<2, spacedim> a_cov,
                           const Tensor<2, dim, Tensor<1,spacedim>> da_cov)
    :
    c_1(c1),
    c_2(c2),
    thickness(h),
    a_cov_ref(a_cov),
    da_cov_ref(da_cov),
    am_cov_ref(metric_covariant(a_cov_ref)),
    am_contra_ref ( metric_contravariant(am_cov_ref))
    {
        Tensor<1, spacedim> a3_t = cross_product_3d(a_cov[0], a_cov[1]);
        double a3_bar = a3_t.norm();
        Tensor<1, dim, Tensor<1, spacedim>> a3_t_da;
        for (unsigned int i = 0; i < dim; ++i) {
            a3_t_da[i] = cross_product_3d(da_cov[0][i], a_cov[1]) + cross_product_3d(a_cov[0], da_cov[1][i]);
        }
        Tensor<1, dim> a3_bar_da;
        for (unsigned int i = 0; i < dim; ++i) {
            a3_bar_da[i] = scalar_product(a3_t, a3_t_da[i])/a3_bar;
        }
        for (unsigned int i = 0; i < dim; ++i) {
            da3_ref[i] = a3_t_da[i] / a3_bar -  ( a3_bar_da[i] * a3_t) / (a3_bar * a3_bar);
        }
        a_cov_def = a_cov_ref;
        da_cov_def = da_cov_ref;
    }
    
    Tensor<2,dim>  get_tau(const double C_33,
                           const Tensor<2,dim> gm_contra_ref,
                           const Tensor<2,dim> gm_cov_def,
                           const Tensor<2,dim> gm_contra_def);
    
    Tensor<4,dim>  get_elastic_tensor(const double C_33,
                                      const Tensor<2,dim> gm_contra_ref,
                                      const Tensor<2,dim> gm_cov_def,
                                      const Tensor<2,dim> gm_contra_def);
    
    void update(const Tensor<1, dim, Tensor<1,spacedim>> delta_u_der, /* du_{,a} */
                const Tensor<2, dim, Tensor<1,spacedim>> delta_u_der2 /* du_{,ab} */)
    {
        u_der += delta_u_der;
        u_der2 += delta_u_der2;
        for (unsigned int ia = 0; ia < dim; ++ia){
            a_cov_def[ia] += delta_u_der[ia]; // a_alpha = bar{a_alpha} + u_{,alpha}
            for (unsigned int ib = 0; ib < dim; ++ib){
                da_cov_def[ia][ib] += delta_u_der2[ia][ib]; // a_{alpha,beta} = bar{a_{alpha,beta}} + u_{,alpha beta}
            }
        }
    };
    
    std::pair<std::vector<Tensor<2,dim>>, std::vector<Tensor<4,dim>>> get_integral_tensors();
    
    Tensor<2, spacedim> get_deformed_covariant_bases()
    {
        return a_cov_def;
    }
    
    Tensor<2, dim, Tensor<1,spacedim>> get_deformed_covariant_bases_deriv()
    {
        return da_cov_def;
    }
    
private:
    const double c_1,c_2;
    // covariant base  a_1, a_2, a_3;
    const Tensor<2, spacedim> a_cov_ref; // a_i = x_{,i} , i = 1,2,3
    // derivatives of covariant base;
    const Tensor<2, dim, Tensor<1,spacedim>> da_cov_ref; // a_{i,j} = x_{,ij} , i,j = 1,2
    // deformed covariant base  a_1, a_2, a_3;
    Tensor<2, spacedim> a_cov_def; // a_i = x_{,i} , i = 1,2,3
    // deformed derivatives of covariant base a_1, a_2;
    Tensor<2, dim, Tensor<1,spacedim>> da_cov_def; // a_{i,j} = x_{,ij} , i,j = 1,2
    // derivatives of a_3
    Tensor<1, dim, Tensor<1,spacedim>> da3_ref;
    // covariant metric tensor
    const Tensor<2,dim> am_cov_ref;
    // contravariant metric tensor
    const Tensor<2,dim> am_contra_ref;
    
    const double thickness;
    
    const QGauss<dim-1> Qh = QGauss<dim-1>(2);
    
    Tensor<1, dim, Tensor<1,spacedim>> u_der ;
    
    Tensor<2, dim, Tensor<1,spacedim>> u_der2;
    
};



template<int dim, int spacedim>
Tensor<2,dim> material_mooney_rivlin<dim,spacedim> :: get_tau(const double C_33,
                                                              const Tensor<2,dim> gm_contra_ref,
                                                              const Tensor<2,dim> gm_cov_def,
                                                              const Tensor<2,dim> gm_contra_def)
{
    Tensor<2,dim> tau;
    for (unsigned int ia = 0; ia < dim; ++ia){
        for (unsigned int ib = 0; ib < dim; ++ib){
            tau[ia][ib] += 2. * (c_1 * gm_contra_ref[ia][ib]) - 2. * c_1 * C_33 * gm_contra_def[ia][ib];
            for (unsigned int ic = 0; ic < dim; ++ic){
                for (unsigned int id = 0; id < dim; ++id){
                    tau[ia][ib] += 2. * ( c_2 * ( gm_cov_def[ic][id] * gm_contra_ref[ic][id] * gm_contra_ref[ia][ib] -  gm_cov_def[ic][id] * gm_contra_ref[ia][ic] * gm_contra_ref[id][ib] )) - 2. * (  c_2 * ( gm_cov_def[ic][id] * gm_contra_ref[ic][id] ) ) * C_33 * gm_contra_def[ia][ib];
                    
                }
            }
        }
    }
    
    return tau;
}



template<int dim, int spacedim>
Tensor<4, dim> material_mooney_rivlin<dim,spacedim> ::get_elastic_tensor(const double C_33,
                                                                         const Tensor<2,dim> gm_contra_ref,
                                                                         const Tensor<2,dim> gm_cov_def,
                                                                         const Tensor<2,dim> gm_contra_def)
{
    Tensor<4, dim> elastic_tensor;
    Tensor<4, dim> d2psi_d2;
    Tensor<2, dim> dpsi_d33dab;
    double dpsi_d33 = c_1;
    for (unsigned int ia = 0; ia < dim; ++ia) {
        for (unsigned int ib = 0; ib < dim; ++ib) {
            dpsi_d33dab[ia][ib] += c_2 * gm_contra_ref[ia][ib];
            dpsi_d33 += c_2 * ( gm_cov_def[ia][ib] * gm_contra_ref[ia][ib] );
            for (unsigned int ic = 0; ic < dim; ++ic) {
                for (unsigned int id = 0; id < dim; ++id) {
                    d2psi_d2[ia][ib][ic][id] += c_2 * gm_contra_ref[ia][ib] * gm_contra_ref[ic][id] - 0.5 * c_2 * (gm_contra_ref[ia][ic] * gm_contra_ref[ib][id] + gm_contra_ref[ia][id] * gm_contra_ref[ib][ic]);
                }
            }
        }
    }
    
    for (unsigned int ia = 0; ia < dim; ++ia) {
        for (unsigned int ib = 0; ib < dim; ++ib) {
            for (unsigned int ic = 0; ic < dim; ++ic) {
                for (unsigned int id = 0; id < dim; ++id) {
                    elastic_tensor[ia][ib][ic][id] += 4 * d2psi_d2[ia][ib][ic][id] - 4 * dpsi_d33dab[ia][ib] * C_33 * gm_contra_def[ic][id] - 4 * dpsi_d33dab[ic][id] * C_33 * gm_contra_def[ia][ib] + 2 * dpsi_d33 * C_33 * ( 2 * gm_contra_def[ia][ib] * gm_contra_def[ic][id] + gm_contra_def[ia][ic] * gm_contra_def[ib][id] + gm_contra_def[ia][id] * gm_contra_def[ib][ic] );
                }
            }
        }
    }
    
    return elastic_tensor;
}



template<int dim, int spacedim>
std::pair<std::vector<Tensor<2,dim>>, std::vector<Tensor<4,dim>>> material_mooney_rivlin< dim, spacedim >:: get_integral_tensors()
{
    std::vector<Tensor<2,dim>> resultants(2);
    std::vector<Tensor<4,dim>> D_tensors(3);
    for (unsigned int iq_1d = 0; iq_1d < Qh.size(); ++iq_1d) {
        double u_t = Qh.get_points()[iq_1d][0];
        double w_t = Qh.get_weights()[iq_1d];
        double zeta = thickness * (u_t - 0.5);
        Tensor<2,spacedim> g_cov_ref;
        g_cov_ref[0] = a_cov_ref[0] + zeta * da3_ref[0];
        g_cov_ref[1] = a_cov_ref[1] + zeta * da3_ref[1];
        g_cov_ref[2] = cross_product_3d(g_cov_ref[0], g_cov_ref[1]);
        double J_ratio = g_cov_ref[2].norm()/cross_product_3d(a_cov_ref[0],a_cov_ref[1]).norm();
        g_cov_ref[2] = a_cov_ref[2]; // Kirchhoff-Love assumption
        
        Tensor<2, dim> gm_cov_ref = metric_covariant(g_cov_ref); // gm_ab
        Tensor<2, dim> gm_contra_ref = metric_contravariant(gm_cov_ref);
        
        Tensor<2, spacedim> g_cov_def = g_cov_ref;
        g_cov_def[0] += u_der[0];
        g_cov_def[1] += u_der[1];
        Tensor<2, dim> gm_cov_def = metric_covariant(g_cov_def);
        Tensor<2, dim> gm_contra_def = metric_contravariant(gm_cov_def);
        
        // for imcompressible material
        double g_33 = determinant(gm_cov_ref)/determinant(gm_cov_def); // J_0^{-2}
        
        Tensor<2, dim> stress_tensor = get_tau(g_33, gm_contra_ref,gm_cov_def, gm_contra_def);
        Tensor<4, dim> elastic_tensor = get_elastic_tensor(g_33, gm_contra_ref, gm_cov_def, gm_contra_def);
        
        for (unsigned int ia = 0; ia < dim; ++ia) {
            for (unsigned int ib = 0; ib < dim; ++ib) {
                resultants[0][ia][ib] += stress_tensor[ia][ib] * thickness * J_ratio * w_t;
                resultants[1][ia][ib] += stress_tensor[ia][ib] * zeta * thickness * J_ratio * w_t;
                for (unsigned int ic = 0; ic < dim; ++ic) {
                    for (unsigned int id = 0; id < dim; ++id) {
                        D_tensors[0][ia][ib][ic][id] += elastic_tensor[ia][ib][ic][id] * J_ratio * thickness * w_t;
                        D_tensors[1][ia][ib][ic][id] += elastic_tensor[ia][ib][ic][id] * zeta * J_ratio * thickness * w_t;
                        D_tensors[2][ia][ib][ic][id] += elastic_tensor[ia][ib][ic][id] * zeta  * zeta * J_ratio * thickness * w_t;
                    }
                }
            }
        }
    }//loop over thickness quadrature points
    return std::make_pair(resultants, D_tensors);
}



template<int dim, int spacedim>
class PointHistory
{
public:
    PointHistory()
    {}
    
    virtual ~PointHistory()
    {}
    
    std::string return_material_type(){return material_type;}
    
    void setup_cell_qp (const double h,
                        const Tensor<2, spacedim> a_cov,
                        const Tensor<2, dim, Tensor<1,spacedim>> da_cov,
                        const double mu)
    {
        material.reset(new material_neo_hookean<dim,spacedim>(mu, h, a_cov, da_cov));
    }
    
    void update_cell_qp(const Tensor<1, dim, Tensor<1,spacedim>> delta_u_der, /* du_{,a} */
                        const Tensor<2, dim, Tensor<1,spacedim>> delta_u_der2 /* du_{,ab} */)
    {
        material->update(delta_u_der, delta_u_der2);
    }
    
    std::pair<std::vector<Tensor<2,dim>>, std::vector<Tensor<4,dim>>> get_integral_tensors(){
        return material->get_integral_tensors();
    }
    
    Tensor<2, spacedim> get_deformed_covariant_bases(){
        return material->get_deformed_covariant_bases();
    }
    
    Tensor<2, dim, Tensor<1,spacedim>> get_deformed_covariant_bases_deriv(){
        return material->get_deformed_covariant_bases_deriv();
    }
    
private:
    std::shared_ptr< material_neo_hookean<dim,spacedim> > material;
    std::string material_type = "neo_hookean";
};



template<int dim, int spacedim>
class PointHistory_MR
{
public:
    PointHistory_MR()
    {}
    
    virtual ~PointHistory_MR()
    {}
    
    std::string return_material_type(){return material_type;}
    
    void setup_cell_qp (const double h,
                        const Tensor<2, spacedim> a_cov,
                        const Tensor<2, dim, Tensor<1,spacedim>> da_cov,
                        const double c_1,
                        const double c_2)
    {
        material.reset(new material_mooney_rivlin<dim,spacedim>(c_1, c_2, h, a_cov, da_cov));
    }
    
    void update_cell_qp(const Tensor<1, dim, Tensor<1,spacedim>> delta_u_der, /* du_{,a} */
                        const Tensor<2, dim, Tensor<1,spacedim>> delta_u_der2 /* du_{,ab} */)
    {
        material->update(delta_u_der, delta_u_der2);
    }
    
    std::pair<std::vector<Tensor<2,dim>>, std::vector<Tensor<4,dim>>> get_integral_tensors(){
        return material->get_integral_tensors();
    }
    
    Tensor<2, spacedim> get_deformed_covariant_bases(){
        return material->get_deformed_covariant_bases();
    }
    
    Tensor<2, dim, Tensor<1,spacedim>> get_deformed_covariant_bases_deriv(){
        return material->get_deformed_covariant_bases_deriv();
    }
    
private:
    std::shared_ptr< material_mooney_rivlin<dim,spacedim> > material;
    std::string material_type = "mooney_rivlin";
};







template<int dim, int spacedim>
class tangent_derivatives
{
public:
    tangent_derivatives(const double ishape_fun, const Tensor<1, dim> ishape_grad, const Tensor<2,dim> ishape_hessian, const double jshape_fun, const Tensor<1, dim> jshape_grad, const Tensor<2,dim> jshape_hessian, const Tensor<2, spacedim> a_cov, const Tensor<2, dim, Tensor<1,spacedim>> da_cov, const unsigned int dof_i, const unsigned int dof_j)
    :
    i_shape(ishape_fun),
    i_shape_deriv(ishape_grad),
    i_shape_deriv2(ishape_hessian),
    j_shape(jshape_fun),
    j_shape_deriv(jshape_grad),
    j_shape_deriv2(jshape_hessian),
    a_cov(a_cov),
    da_cov(da_cov),
    r(dof_i),
    s(dof_j)
    {
        u_r[r%3] = i_shape;
        r_r[r%3] = i_shape;
        
        u_s[s%3] = j_shape;
        r_s[s%3] = j_shape;
        
        for (unsigned int i = 0; i < dim; ++i) {
            a_cov_ar[i][r%3] = i_shape_deriv[i];
            a_cov_as[i][s%3] = j_shape_deriv[i];
            
            for (unsigned int j = 0; j < dim; ++j) {
                a_cov_abr[i][j][r%3] = i_shape_deriv2[i][j];
                a_cov_abs[i][j][s%3] = j_shape_deriv2[i][j];
            }
        }
        
        Tensor<1, spacedim> a3_t = cross_product_3d(a_cov[0], a_cov[1]);
        double a3_bar = a3_t.norm();
        Tensor<1, dim, Tensor<1, spacedim>> a3_t_da;
        for (unsigned int i = 0; i < dim; ++i) {
            a3_t_da[i] = cross_product_3d(da_cov[0][i], a_cov[1]) + cross_product_3d(a_cov[0], da_cov[1][i]);
        }
        Tensor<1, dim> a3_bar_da;
        for (unsigned int i = 0; i < dim; ++i) {
            a3_bar_da[i] = scalar_product(a3_t, a3_t_da[i])/a3_bar;
        }
        for (unsigned int i = 0; i < dim; ++i) {
            a3_da[i] = a3_t_da[i] / a3_bar -  ( a3_bar_da[i] * a3_t) / (a3_bar * a3_bar);
        }
        
        Tensor<1, spacedim> a3_t_dr = cross_product_3d(a_cov_ar[0], a_cov[1]) + cross_product_3d(a_cov[0], a_cov_ar[1]);
        Tensor<1, spacedim> a3_t_ds = cross_product_3d(a_cov_as[0], a_cov[1]) + cross_product_3d(a_cov[0], a_cov_as[1]);
        double a3_bar_dr = scalar_product(a3_t, a3_t_dr)/a3_bar;
        double a3_bar_ds = scalar_product(a3_t, a3_t_ds)/a3_bar;
        
        Tensor<1, spacedim> a3_t_drs = cross_product_3d(a_cov_ar[0], a_cov_as[1]) + cross_product_3d(a_cov_as[0], a_cov_ar[1]);
        double a3_bar_drs = scalar_product(a3_t_ds, a3_t_dr)/ a3_bar + scalar_product(a3_t, a3_t_drs)/ a3_bar - (a3_bar_ds * a3_bar_dr)/ a3_bar;
        a3_dr = a3_t_dr / a3_bar - a3_bar_dr * a3_t/ (a3_bar * a3_bar);
        a3_ds = a3_t_ds / a3_bar - a3_bar_ds * a3_t/ (a3_bar * a3_bar);
        a3_drs = a3_t_drs / a3_bar - a3_bar_drs * a3_t /(a3_bar * a3_bar) - a3_bar_dr * a3_t_ds / (a3_bar * a3_bar) - a3_bar_ds * a3_t_dr / (a3_bar * a3_bar) + 2 * a3_bar_dr * a3_bar_ds * a3_t / (a3_bar * a3_bar * a3_bar);
        
        for (unsigned int ia = 0; ia < dim; ++ia) {
            for (unsigned int ib = 0; ib < dim; ++ib) {
                membrane_strain_dr[ia][ib] = 0.5 * ( scalar_product( a_cov_ar[ia], a_cov[ib]) +  scalar_product( a_cov_ar[ib], a_cov[ia]) );
                membrane_strain_ds[ia][ib] = 0.5 * ( scalar_product( a_cov_as[ia], a_cov[ib]) +  scalar_product( a_cov_as[ib], a_cov[ia]) );
                membrane_strain_drs[ia][ib] = 0.5 * ( scalar_product( a_cov_ar[ia], a_cov_as[ib]) + scalar_product( a_cov_ar[ib], a_cov_as[ia]) );
                
                bending_strain_dr[ia][ib] = - ( scalar_product(a_cov_abr[ia][ib], a_cov[2]) + scalar_product(da_cov[ia][ib], a3_dr) );
                bending_strain_ds[ia][ib] = - ( scalar_product(a_cov_abs[ia][ib], a_cov[2]) + scalar_product(da_cov[ia][ib], a3_ds) );
                bending_strain_drs[ia][ib] = - ( scalar_product(a_cov_abr[ia][ib], a3_ds) + scalar_product(a_cov_abs[ia][ib], a3_dr) + scalar_product(da_cov[ia][ib], a3_drs) );
            }
        }
    }
    
    Tensor<2, dim> get_membrane_strain_dr(){return membrane_strain_dr;};
    Tensor<2, dim> get_membrane_strain_ds(){return membrane_strain_ds;};
    Tensor<2, dim> get_membrane_strain_drs(){return membrane_strain_drs;};
    
    Tensor<2, dim> get_bending_strain_dr(){return bending_strain_dr;};
    Tensor<2, dim> get_bending_strain_ds(){return bending_strain_ds;};
    Tensor<2, dim> get_bending_strain_drs(){return bending_strain_drs;};
    
    Tensor<1,spacedim> get_u_r(){return u_r;};
    Tensor<1,spacedim> get_r_r(){return r_r;};
    Tensor<1,spacedim> get_u_s(){return u_s;};
    Tensor<1,spacedim> get_r_s(){return r_s;};
    
private:
    const double i_shape;
    const Tensor<1, dim> i_shape_deriv;
    const Tensor<2, dim> i_shape_deriv2;
    const double j_shape;
    const Tensor<1, dim> j_shape_deriv;
    const Tensor<2, dim> j_shape_deriv2;
    const Tensor<2, spacedim> a_cov;
    const Tensor<2, dim, Tensor<1,spacedim>> da_cov;
    const unsigned int r,s;
    
    Tensor<1, dim, Tensor<1, spacedim>> a_cov_ar;
    Tensor<2, dim, Tensor<1, spacedim>> a_cov_abr;
    Tensor<1, dim, Tensor<1, spacedim>> a_cov_as;
    Tensor<2, dim, Tensor<1, spacedim>> a_cov_abs;
    Tensor<1, dim, Tensor<1, spacedim>> a3_da;
    
    Tensor<2, dim> membrane_strain_dr;
    Tensor<2, dim> membrane_strain_ds;
    Tensor<2, dim> membrane_strain_drs;
    
    Tensor<2, dim> bending_strain_dr;
    Tensor<2, dim> bending_strain_ds;
    Tensor<2, dim> bending_strain_drs;
    
    Tensor<1, spacedim> a3_dr;
    Tensor<1, spacedim> a3_ds;
    Tensor<1, spacedim> a3_drs;
    
    Tensor<1,spacedim> u_r, r_r, u_s, r_s;
};



template <int dim, int spacedim>
class Nonlinear_shell
{
public:
    Nonlinear_shell(Triangulation<dim,spacedim> &tria);
    ~Nonlinear_shell();
    void run();
private:
    void   setup_system();
    void   assemble_system(const bool initial_step = false);
    void   assemble_mass_matrix_and_rhs();
    void   assemble_boundary_mass_matrix_and_rhs();
    void   make_constrains();
    void   solve();
    void   solve_disp_coeff();
    void   initialise_data(hp::FEValues<dim,spacedim> hp_fe_values);
    double get_error_residual();
    void   nonlinear_solver(const bool initial_step = false);
    void   make_constrains(const unsigned int newton_iteration);
    
    //    Triangulation<dim,spacedim> mesh;
    hp::DoFHandler<dim,spacedim> dof_handler;
    hp::FECollection<dim,spacedim> fe_collection;
    hp::MappingCollection<dim,spacedim> mapping_collection;
    hp::QCollection<dim> q_collection;
    hp::QCollection<dim> boundary_q_collection;
    SparsityPattern      sparsity_pattern;
    AffineConstraints<double> constraints;
    std::vector<PointHistory<dim,spacedim>>  quadrature_point_history;
    std::string material_type = "neo_hookean";
    SparseMatrix<double> system_matrix;
    SparseMatrix<double> mass_matrix;
    Vector<double> internal_force_rhs;
    Vector<double> external_force_rhs;
    Vector<double> force_rhs;
    SparseMatrix<double> boundary_mass_matrix;
    Vector<double> boundary_value_rhs;
    Vector<double> boundary_edge_load_rhs;
    Vector<double> vec_disp_coeff;
    std::vector<double> disp_coeff;
//    std::vector<double> disp_coeff = { 2.13756, 3.85704e-26, 3.85704e-26, 2.20145, 0.446109, 4.1197e-26, 2.20145, 4.1197e-26, 0.446109, 2.29638, 0.470389, 0.470389, 2.04083, 0.853123, 4.00631e-26, 2.10866, 0.892031, 0.437097, 2.04083, 4.00631e-26, 0.853123, 2.10866, 0.437097, 0.892031, 1.95951, 0.83616, 0.83616, 1.86507, 1.24712, 4.00836e-26, 1.91901, 1.29146, 0.410774, 1.57795, 1.57795, 4.02178e-26, 1.61676, 1.61676, 0.359534, 1.79893, 1.21746, 0.789259, 1.53982, 1.53982, 0.694508, 1.86507, 4.00836e-26, 1.24712, 1.91901, 0.410774, 1.29146, 1.79893, 0.789259, 1.21746, 1.53982, 0.694508, 1.53982, 1.66248, 1.14715, 1.14715, 1.45593, 1.45593, 1.02319, 1.45593, 1.02319, 1.45593, 1.31236, 1.31236, 1.31236, 3.85704e-26, 2.13756, 3.85704e-26, 4.1197e-26, 2.20145, 0.446109, 0.446109, 2.20145, 4.1197e-26, 0.470389, 2.29638, 0.470389, 4.00631e-26, 2.04083, 0.853123, 0.437097, 2.10866, 0.892031, 0.853123, 2.04083, 4.00631e-26, 0.892031, 2.10866, 0.437097, 0.83616, 1.95951, 0.83616, 4.00836e-26, 1.86507, 1.24712, 0.410774, 1.91901, 1.29146, 4.02178e-26, 1.57795, 1.57795, 0.359534, 1.61676, 1.61676, 0.789259, 1.79893, 1.21746, 0.694508, 1.53982, 1.53982, 1.24712, 1.86507, 4.00836e-26, 1.29146, 1.91901, 0.410774, 1.21746, 1.79893, 0.789259, 1.14715, 1.66248, 1.14715, 1.02319, 1.45593, 1.45593, 3.85704e-26, 3.85704e-26, 2.13756, 0.446109, 4.1197e-26, 2.20145, 4.1197e-26, 0.446109, 2.20145, 0.470389, 0.470389, 2.29638, 0.853123, 4.00631e-26, 2.04083, 0.892031, 0.437097, 2.10866, 4.00631e-26, 0.853123, 2.04083, 0.437097, 0.892031, 2.10866, 0.83616, 0.83616, 1.95951, 1.24712, 4.00836e-26, 1.86507, 1.29146, 0.410774, 1.91901, 1.57795, 4.02178e-26, 1.57795, 1.61676, 0.359534, 1.61676, 1.21746, 0.789259, 1.79893, 4.00836e-26, 1.24712, 1.86507, 0.410774, 1.29146, 1.91901, 0.789259, 1.21746, 1.79893, 1.14715, 1.14715, 1.66248 };
    Vector<double> vec_pressure_coeff;
    Vector<double> vec_values;
    std::vector<types::global_dof_index> constrained_dof_indices;
    std::vector<types::global_dof_index> fix_dof_indices;
    unsigned int total_q_points;
    const double tolerance = 1e-9;
    const double thickness = 0.1;
    //    const double mu = 4.225e5, c_1 = 0.4375*mu, c_2 = 0.0625*mu;
    const double mu = 4.225e5;
    const QGauss<dim-1> Qthickness = QGauss<dim-1>(2);
    const double penalty_factor = 10e30;
    double delta_radius = 0.4;
};



template <int dim, int spacedim>
Nonlinear_shell<dim, spacedim>::Nonlinear_shell(Triangulation<dim,spacedim> &tria)
:
dof_handler(tria)
{}



template <int dim, int spacedim>
Nonlinear_shell<dim, spacedim>::~Nonlinear_shell()
{
    dof_handler.clear();
}



template <int dim, int spacedim>
Triangulation<dim,spacedim> set_mesh( std::string type )
{
    Triangulation<dim,spacedim> mesh;
    if (type == "roof") {
        static CylindricalManifold<dim,spacedim> surface_description;
        std::string mfile = "/Users/zhaoweiliu/Documents/geometries/roof.msh";
        GridIn<2,3> grid_in;
        grid_in.attach_triangulation(mesh);
        std::ifstream file(mfile.c_str());
        Assert(file, ExcFileNotOpen(mfile.c_str()));
        grid_in.read_msh(file);
        mesh.set_all_manifold_ids(0);
        mesh.set_manifold (0, surface_description);
        mesh.refine_global(1);
    }else if (type == "sphere") {
        static SphericalManifold<dim,spacedim> surface_description;
        {
            Triangulation<spacedim> volume_mesh;
            GridGenerator::hyper_ball(volume_mesh);
            std::set<types::boundary_id> boundary_ids;
            boundary_ids.insert (0);
            GridGenerator::extract_boundary_mesh (volume_mesh, mesh, boundary_ids);
        }
        mesh.set_all_manifold_ids(0);
        mesh.set_manifold (0, surface_description);
        mesh.refine_global(3);
        GridTools::scale(10., mesh);
    }else if (type == "cylinder"){
        static CylindricalManifold<dim,spacedim> surface_description;
        {
            Triangulation<spacedim> volume_mesh;
            GridGenerator::cylinder(volume_mesh,300,300);
            std::set<types::boundary_id> boundary_ids;
            boundary_ids.insert(0);
            GridGenerator::extract_boundary_mesh (volume_mesh, mesh, boundary_ids);
        }
        mesh.set_all_manifold_ids(0);
        mesh.set_manifold (0, surface_description);
        mesh.refine_global(4);
    }else if (type == "plate")
    {
        std::string mfile = "/Users/zhaoweiliu/Documents/geometries/plate_4_2.msh";
        GridIn<2,3> grid_in;
        grid_in.attach_triangulation(mesh);
        std::ifstream file(mfile.c_str());
        Assert(file, ExcFileNotOpen(mfile.c_str()));
        grid_in.read_msh(file);
        mesh.refine_global(1);
    }else if (type == "hemisphere")
    {
        static SphericalManifold<dim,spacedim> surface_description;
        {
            Triangulation<spacedim> volume_mesh;
            GridGenerator::half_hyper_ball(volume_mesh);
            std::set<types::boundary_id> boundary_ids;
            boundary_ids.insert (0);
            GridGenerator::extract_boundary_mesh (volume_mesh, mesh,
                                                  boundary_ids);
        }
        mesh.set_all_manifold_ids(0);
        mesh.set_manifold (0, surface_description);
        mesh.refine_global(1);
        GridTools::scale(10., mesh);
    }else if (type == "quarter_sphere")
    {
        static SphericalManifold<dim,spacedim> surface_description;
        {
            Triangulation<spacedim> volume_mesh;
            GridGenerator::quarter_hyper_ball(volume_mesh);
            std::set<types::boundary_id> boundary_ids;
            boundary_ids.insert (0);
            GridGenerator::extract_boundary_mesh (volume_mesh, mesh, boundary_ids);
        }
        mesh.set_all_manifold_ids(0);
        mesh.set_manifold (0, surface_description);
        mesh.refine_global(2);
        GridTools::scale(10., mesh);
    }else if (type == "cooks_membrane")
    {
        cooks_membrane(mesh);
        GridTools::scale(1e-3, mesh);
    }
    std::cout << "   Number of active cells: " << mesh.n_active_cells()
    << std::endl
    << "   Total number of cells: " << mesh.n_cells()
    << std::endl;
    std::ofstream output_file("test_mesh.vtu");
    GridOut().write_vtu (mesh, output_file);
    return mesh;
}



template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim> :: setup_system()
{
    catmull_clark_create_fe_quadrature_and_mapping_collections_and_distribute_dofs(dof_handler,fe_collection,vec_values,mapping_collection,q_collection,boundary_q_collection,3);
    std::cout << "   Number of dofs: " << dof_handler.n_dofs()
    << std::endl;
    DynamicSparsityPattern dynamic_sparsity_pattern(dof_handler.n_dofs());
    //    constraints.clear();
    DoFTools::make_sparsity_pattern(dof_handler, dynamic_sparsity_pattern, constraints);
    sparsity_pattern.copy_from(dynamic_sparsity_pattern);
    std::ofstream out("CC_sparsity_pattern.svg");
    sparsity_pattern.print_svg(out);
    system_matrix.reinit(sparsity_pattern);
    boundary_mass_matrix.reinit(sparsity_pattern);
    mass_matrix.reinit(sparsity_pattern);
    internal_force_rhs.reinit(dof_handler.n_dofs());
    external_force_rhs.reinit(dof_handler.n_dofs());
    boundary_value_rhs.reinit(dof_handler.n_dofs());
    boundary_edge_load_rhs.reinit(dof_handler.n_dofs());
    vec_disp_coeff.reinit(dof_handler.n_dofs());
    vec_pressure_coeff.reinit(dof_handler.n_dofs());
}



template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim> :: initialise_data(hp::FEValues<dim,spacedim> hp_fe_values)
{
    total_q_points = 0;
    std::cout << "Setting up quadrature point data..." << std::endl;
    for (const auto &cell : dof_handler.active_cell_iterators()){
        hp_fe_values.reinit(cell);
        const FEValues<dim,spacedim> &fe_values = hp_fe_values.get_present_fe_values();
        total_q_points += fe_values.n_quadrature_points;
    }
    
    quadrature_point_history.resize(total_q_points);
    
    unsigned int history_index = 0;
    for (const auto &cell : dof_handler.active_cell_iterators()){
        hp_fe_values.reinit(cell);
        const FEValues<dim,spacedim> &fe_values = hp_fe_values.get_present_fe_values();
        cell->set_user_pointer(&quadrature_point_history[history_index]);
        history_index += fe_values.n_quadrature_points;
    }
    Assert(history_index == quadrature_point_history.size(),ExcInternalError());
    std::cout << "Finish setting up quadrature point data." << std::endl;
}



template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim> :: assemble_system(const bool initial_step)
{
    hp::FEValues<dim,spacedim> hp_fe_values(mapping_collection, fe_collection, q_collection,update_values|update_quadrature_points|update_jacobians|update_jacobian_grads|update_inverse_jacobians| update_gradients|update_hessians|update_jacobian_pushed_forward_grads|update_JxW_values|update_normal_vectors);
    
    FullMatrix<double> cell_mass_matrix;
    FullMatrix<double> cell_system_matrix;
    Vector<double>     cell_internal_force_rhs;
    Vector<double>     cell_external_force_rhs;
    std::vector<types::global_dof_index> local_dof_indices;
    
    if(initial_step == true){
        initialise_data(hp_fe_values);
    }
    double area = 0;
    for (const auto &cell : dof_handler.active_cell_iterators())
    {
        const unsigned int dofs_per_cell = cell->get_fe().dofs_per_cell;
        local_dof_indices.resize(dofs_per_cell);
        cell->get_dof_indices(local_dof_indices);
        
        hp_fe_values.reinit(cell);
        const FEValues<dim,spacedim> &fe_values = hp_fe_values.get_present_fe_values();
        
        cell_mass_matrix.reinit(dofs_per_cell, dofs_per_cell);
        cell_mass_matrix = 0;
        cell_system_matrix.reinit(dofs_per_cell, dofs_per_cell);
        cell_system_matrix = 0;
        cell_internal_force_rhs.reinit(dofs_per_cell);
        cell_internal_force_rhs = 0;
        cell_external_force_rhs.reinit(dofs_per_cell);
        cell_external_force_rhs = 0;
//        PointHistory_MR<dim,spacedim> *lqph = reinterpret_cast<PointHistory_MR<dim,spacedim>*>(cell->user_pointer());
        PointHistory<dim,spacedim> *lqph = reinterpret_cast<PointHistory<dim,spacedim>*>(cell->user_pointer());
        Assert(lqph >= &quadrature_point_history.front(), ExcInternalError());
        Assert(lqph <= &quadrature_point_history.back(), ExcInternalError());
        
        for (unsigned int q_point = 0; q_point < fe_values.n_quadrature_points;
             ++q_point)
        {
            // covariant base  a_1, a_2, a_3;
            Tensor<2, spacedim> a_cov_ref; // a_i = x_{,i} , i = 1,2,3
            // derivatives of covariant base;
            Tensor<2, dim, Tensor<1,spacedim>> da_cov_ref; // a_{i,j} = x_{,ij} , i,j = 1,2
            auto jacobian_ref = fe_values.jacobian(q_point);
            
            for (unsigned int id = 0; id < spacedim; ++id){
                a_cov_ref[0][id] = jacobian_ref[id][0];
                a_cov_ref[1][id] = jacobian_ref[id][1];
            }
            a_cov_ref[2] = cross_product_3d(a_cov_ref[0], a_cov_ref[1]);
            double detJ_ref = a_cov_ref[2].norm();
            a_cov_ref[2] = a_cov_ref[2]/detJ_ref;
            
            auto jacobian_grad_ref = fe_values.jacobian_grad(q_point);
            for (unsigned int jj = 0; jj < dim; ++jj)
            {
                for (unsigned int kk = 0; kk < spacedim; ++kk)
                {
                    da_cov_ref[0][jj][kk] = jacobian_grad_ref[kk][0][jj];
                    da_cov_ref[1][jj][kk] = jacobian_grad_ref[kk][1][jj];
                }
            }
            if(initial_step == true){
                lqph[q_point].setup_cell_qp(thickness, a_cov_ref, da_cov_ref, mu);
            }
            
            std::vector<double> shape_vec(dofs_per_cell);
            std::vector<Tensor<1, dim>> shape_der_vec(dofs_per_cell);
            std::vector<Tensor<2, dim>> shape_der2_vec(dofs_per_cell);
            
            Tensor<1, dim, Tensor<1,spacedim>> u_der; // u_{,a}
            Tensor<2, dim, Tensor<1,spacedim>> u_der2; // u_{,ab}
            
            for (unsigned int i_shape = 0; i_shape < dofs_per_cell; ++i_shape) {
                // compute first and second grad of i_shape function
                double i_shape_vlaue = fe_values.shape_value(i_shape, q_point);
                Tensor<1, spacedim> i_shape_grad = fe_values.shape_grad(i_shape, q_point);
                Tensor<2, spacedim> i_shape_hessian = fe_values.shape_hessian(i_shape, q_point);
                Tensor<1, dim> i_shape_der; // N_{,a}
                Tensor<2, dim> i_shape_der2; // N_{,ab}
                // transform to parametric domain
                for (unsigned int id = 0; id < dim; ++id){
                    for (unsigned int kd = 0; kd < spacedim; ++kd){
                        i_shape_der[id] += i_shape_grad[kd]*jacobian_ref[kd][id];
                        for (unsigned jd = 0; jd < dim; ++jd) {
                            for (unsigned ld = 0; ld < spacedim; ++ld) {
                                i_shape_der2[id][jd] += i_shape_hessian[kd][ld] * jacobian_ref[kd][id] * jacobian_ref[ld][jd];
                            }
                            i_shape_der2[id][jd] += i_shape_grad[kd] * jacobian_grad_ref[kd][id][jd];
                        }
                    }
                }
                shape_vec[i_shape] = i_shape_vlaue;
                shape_der_vec[i_shape] = i_shape_der;
                shape_der2_vec[i_shape] = i_shape_der2;
                for (unsigned int ia = 0; ia < dim; ++ia){
                    u_der[ia][i_shape%3] += i_shape_der[ia] * vec_disp_coeff(local_dof_indices[i_shape]); // u_{,a} = sum N^A_{,a} * U_A
                    for (unsigned int ib = 0; ib < dim; ++ib){
                        u_der2[ia][ib][i_shape%3] += i_shape_der2[ia][ib] * vec_disp_coeff(local_dof_indices[i_shape]); // u_{,ab} = sum N^A_{,ab} * U_A
                    }
                }
            }
            
//            ß®if (initial_step == false)
            {lqph[q_point].update_cell_qp(u_der,u_der2);}
            
            std::pair<std::vector<Tensor<2,dim>>, std::vector<Tensor<4,dim>>> integral_tensors = lqph[q_point].get_integral_tensors();
            std::vector<Tensor<2,dim>> resultants = integral_tensors.first;
//            Tensor<4,dim> D0 = integral_tensors.second[0];
//            Tensor<4,dim> D1 = integral_tensors.second[1];
//            Tensor<4,dim> D2 = integral_tensors.second[2];
//            std::cout <<"S0: \n "<< resultants[0] << std::endl;
//            std::cout <<"S1: \n"<< resultants[1] << std::endl;
            
            Tensor<2, spacedim> a_cov_def = lqph[q_point].get_deformed_covariant_bases();
            double detJ_def = cross_product_3d(a_cov_def[0], a_cov_def[1]).norm(); 
            Tensor<2, dim, Tensor<1,spacedim>> da_cov_def = lqph[q_point].get_deformed_covariant_bases_deriv();
            
            for (unsigned int r_shape = 0; r_shape < dofs_per_cell; ++r_shape) {
                double shape_r = shape_vec[r_shape];
                Tensor<1, dim> shape_r_der = shape_der_vec[r_shape];
                Tensor<2, dim> shape_r_der2 = shape_der2_vec[r_shape];
                
                Tensor<1,spacedim> u_r;
                Tensor<2, dim> membrane_strain_dr;
                Tensor<2, dim> bending_strain_dr;
                
                for (unsigned int s_shape = 0; s_shape < dofs_per_cell; ++s_shape) {
                    double shape_s = shape_vec[s_shape];
                    Tensor<1, dim> shape_s_der = shape_der_vec[s_shape];
                    Tensor<2, dim> shape_s_der2 = shape_der2_vec[s_shape];
                    
                    tangent_derivatives<dim,spacedim> T_derivs(shape_r, shape_r_der, shape_r_der2, shape_s, shape_s_der, shape_s_der2, a_cov_def, da_cov_def, r_shape, s_shape);
                    u_r = T_derivs.get_u_r();
                    membrane_strain_dr = T_derivs.get_membrane_strain_dr();
                    bending_strain_dr  = T_derivs.get_bending_strain_dr();

                    if (r_shape%3 == s_shape%3){
                        cell_system_matrix[r_shape][s_shape] += shape_s * shape_r * fe_values.JxW(q_point);
                    }
                }
                for (unsigned int ia = 0; ia < dim; ++ia) {
                    for (unsigned int ib = 0; ib < dim; ++ib) {
                        cell_internal_force_rhs[r_shape] += (membrane_strain_dr[ia][ib] * resultants[0][ia][ib] + bending_strain_dr[ia][ib] * resultants[1][ia][ib]) * fe_values.JxW(q_point); // f^int
                    }
                }
            }
            area += fe_values.JxW(q_point);
        }// loop over surface quadrature points
        internal_force_rhs.add(local_dof_indices, cell_internal_force_rhs);
        system_matrix.add(local_dof_indices, local_dof_indices, cell_system_matrix);
    } // loop over cells
}



template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim>::assemble_mass_matrix_and_rhs()
{
    mass_matrix = 0;
    boundary_value_rhs = 0;
    hp::FEValues<dim,spacedim> hp_fe_values(mapping_collection, fe_collection, q_collection,update_values|update_quadrature_points|update_jacobians|update_jacobian_grads|update_inverse_jacobians| update_gradients|update_hessians|update_jacobian_pushed_forward_grads|update_JxW_values|update_normal_vectors);

    FullMatrix<double> cell_mass_matrix;
    Vector<double> cell_rhs;
    std::vector<types::global_dof_index> local_dof_indices;
    for (const auto &cell : dof_handler.active_cell_iterators())
    {
        const unsigned int dofs_per_cell = cell->get_fe().dofs_per_cell;
        local_dof_indices.resize(dofs_per_cell);
        cell->get_dof_indices(local_dof_indices);
        hp_fe_values.reinit(cell);
        const FEValues<dim, spacedim> &fe_values = hp_fe_values.get_present_fe_values();
        cell_mass_matrix.reinit(dofs_per_cell, dofs_per_cell);
        cell_rhs.reinit(dofs_per_cell);
        cell_mass_matrix = 0; cell_rhs = 0;
        
        for (unsigned int q_point = 0; q_point < fe_values.n_quadrature_points;
             ++q_point)
        {
//            Point<spacedim> qpt = fe_values.quadrature_point(q_point);
            for (unsigned int i_shape = 0; i_shape < dofs_per_cell; ++i_shape) {
                if ( fe_values.shape_value(i_shape, q_point) > 0) {
                    for (unsigned int j_shape = 0; j_shape < dofs_per_cell; ++j_shape) {
                        if (i_shape%3 == j_shape%3){
                            cell_mass_matrix[i_shape][j_shape] += fe_values.shape_value(i_shape, q_point) * fe_values.shape_value(j_shape, q_point) * fe_values.JxW(q_point);
                        }
                    }
                }
                cell_rhs[i_shape] += delta_radius * fe_values.normal_vector(q_point)[i_shape%3] * fe_values.shape_value(i_shape, q_point) * fe_values.JxW(q_point);
            }
        }// loop over q_points
        mass_matrix.add(local_dof_indices, cell_mass_matrix);
        boundary_value_rhs.add(local_dof_indices, cell_rhs);
    }// loop over elements
}



template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim>::assemble_boundary_mass_matrix_and_rhs()
{
    boundary_mass_matrix = 0;
    boundary_value_rhs = 0;
    boundary_edge_load_rhs = 0;
    hp::FEValues<dim,spacedim> hp_fe_boundary_values(mapping_collection, fe_collection, boundary_q_collection, update_values|update_quadrature_points|update_jacobians|update_jacobian_grads|update_inverse_jacobians| update_gradients|update_hessians|update_jacobian_pushed_forward_grads|update_JxW_values|update_normal_vectors);
    
    FullMatrix<double> cell_b_mass_matrix;
    Vector<double> cell_b_rhs;
    Vector<double> cell_load_rhs;
    std::vector<types::global_dof_index> local_dof_indices;
    double boundary_length = 0.0;
    for (const auto &cell : dof_handler.active_cell_iterators())
    {
        const unsigned int dofs_per_cell = cell->get_fe().dofs_per_cell;
        local_dof_indices.resize(dofs_per_cell);
        cell->get_dof_indices(local_dof_indices);
        hp_fe_boundary_values.reinit(cell);
        const FEValues<dim, spacedim> &b_fe_values = hp_fe_boundary_values.get_present_fe_values();
        cell_b_mass_matrix.reinit(dofs_per_cell, dofs_per_cell);
        cell_b_rhs.reinit(dofs_per_cell);
        cell_load_rhs.reinit(dofs_per_cell);
        cell_b_mass_matrix = 0; cell_b_rhs = 0; cell_load_rhs = 0;
//        std::cout << "cell " << cell->active_cell_index() << " has "<< b_fe_values.n_quadrature_points <<" boundary qpts."<< std::endl;
        if (b_fe_values.n_quadrature_points != 1)
        {
            for (unsigned int q_point = 0; q_point < b_fe_values.n_quadrature_points;
                 ++q_point)
            {
                Point<spacedim> qpt = b_fe_values.quadrature_point(q_point);
//                std::cout << "gauss point " << q_point << " = "<< b_fe_values.get_quadrature().point(q_point) << " weight = " << b_fe_values.get_quadrature().weight(q_point) << " qpt = " << qpt << std::endl;
                // covariant base  a_1, a_2, a_3;
                Tensor<2, spacedim> a_cov; // a_i = x_{,i} , i = 1,2,3
                auto jacobian_ref = b_fe_values.jacobian(q_point);
                
                for (unsigned int id = 0; id < spacedim; ++id){
                    a_cov[0][id] = jacobian_ref[id][0];
                    a_cov[1][id] = jacobian_ref[id][1];
                }
                double tol = 1e-9;
//                if (qpt[0] < tol|| qpt[0] - 2 <tol || qpt[1] < tol || qpt[1] - 2 < 1e-9) {
                if (std::abs(qpt[0]) < tol || std::abs(qpt[1]) < tol || std::abs(qpt[2]) < tol) {
                    double jxw;
                    
                    if (b_fe_values.get_quadrature().point(q_point)[0] == 0 ) {
                        jxw = a_cov[1].norm() * b_fe_values.get_quadrature().weight(q_point);
                    }else if (b_fe_values.get_quadrature().point(q_point)[1] == 0 ){
                        jxw = a_cov[0].norm() * b_fe_values.get_quadrature().weight(q_point);
                    }
//                    boundary_length += jxw;
                    
                    for (unsigned int i_shape = 0; i_shape < dofs_per_cell; ++i_shape) {
                        if ( b_fe_values.shape_value(i_shape, q_point) > tol) {
                            constrained_dof_indices.push_back(local_dof_indices[i_shape]);
                            for (unsigned int j_shape = 0; j_shape < dofs_per_cell; ++j_shape) {
                                if (std::abs(qpt[0]) < tol && b_fe_values.shape_value(j_shape, q_point) > tol) {
                                    if (i_shape%3 == j_shape%3 && i_shape%3 == 0){
                                        cell_b_mass_matrix[i_shape][j_shape] += b_fe_values.shape_value(i_shape, q_point) * b_fe_values.shape_value(j_shape, q_point) * jxw;
                                    }
                                }
                                if (std::abs(qpt[1]) < tol && b_fe_values.shape_value(j_shape, q_point) > tol) {
                                    if (i_shape%3 == j_shape%3 && i_shape%3 == 1){
                                        cell_b_mass_matrix[i_shape][j_shape] += b_fe_values.shape_value(i_shape, q_point) * b_fe_values.shape_value(j_shape, q_point) * jxw;
                                    }
                                }
                                if (std::abs(qpt[2]) < tol && b_fe_values.shape_value(j_shape, q_point) > tol) {
                                    if (i_shape%3 == j_shape%3 && i_shape%3 == 2){
                                        cell_b_mass_matrix[i_shape][j_shape] += b_fe_values.shape_value(i_shape, q_point) * b_fe_values.shape_value(j_shape, q_point) * jxw;
                                    }
                                }
                            }
                        }
                        
//                        for (unsigned int j_shape = 0; j_shape < dofs_per_cell; ++j_shape) {
//                            if ((std::abs(qpt[0]) < tol || std::abs(qpt[0] - 2) < tol ) && i_shape%3 == j_shape%3 && i_shape%3 != 1){
//                                cell_b_mass_matrix[i_shape][j_shape] += b_fe_values.shape_value(i_shape, q_point) * b_fe_values.shape_value(j_shape, q_point) * jxw;
//                            }
//                            if (std::abs(qpt[1]) < tol && i_shape%3 == j_shape%3 && i_shape%3 != 0){
//                                cell_b_mass_matrix[i_shape][j_shape] += b_fe_values.shape_value(i_shape, q_point) * b_fe_values.shape_value(j_shape, q_point) * jxw;
//                            }
//                        }
                    }
                }
                
//                if(std::abs(qpt[0] - 48e-3) < tol){
//                    for (unsigned int i_shape = 0; i_shape < dofs_per_cell; ++i_shape) {
//                        double jxw;
//                        if (b_fe_values.get_quadrature().point(q_point)[0] == 0 ) {
//                            jxw = a_cov[1].norm() * b_fe_values.get_quadrature().weight(q_point);
//                        }else if (b_fe_values.get_quadrature().point(q_point)[1] == 0 ){
//                            jxw = a_cov[0].norm() * b_fe_values.get_quadrature().weight(q_point);
//                        }
//                        if (i_shape%3 == 1){
//                            cell_load_rhs[i_shape] += f_load/(16e-3) * b_fe_values.shape_value(i_shape, q_point) * jxw;
//                        }
//                    }
//                }
            }
            boundary_mass_matrix.add(local_dof_indices, cell_b_mass_matrix);
        }
    }
    std::sort(constrained_dof_indices.begin(), constrained_dof_indices.end());
    auto last = std::unique(constrained_dof_indices.begin(), constrained_dof_indices.end());
    constrained_dof_indices.erase(last, constrained_dof_indices.end());
}




template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim>::make_constrains(){
    assemble_boundary_mass_matrix_and_rhs();
    system_matrix.add(penalty_factor, boundary_mass_matrix);
}



template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim>::solve()
{
    SolverControl            solver_control(20000, 1e-9);
    SolverCG<Vector<double>> solver(solver_control);
    PreconditionSSOR<SparseMatrix<double>> preconditioner;
    preconditioner.initialize(system_matrix);
    solver.solve(system_matrix, vec_pressure_coeff, internal_force_rhs, preconditioner);
}



template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim>::solve_disp_coeff()
{
    SolverControl            solver_control(20000, 1e-9);
    SolverCG<Vector<double>> solver(solver_control);
    PreconditionSSOR<SparseMatrix<double>> preconditioner;
    preconditioner.initialize(mass_matrix);
    solver.solve(mass_matrix, vec_disp_coeff, boundary_value_rhs, preconditioner);
}



template <int dim, int spacedim>
void Nonlinear_shell<dim, spacedim> ::run()
{
    setup_system();
    assemble_boundary_mass_matrix_and_rhs();
//    solve_disp_coeff();
    vec_disp_coeff.reinit(disp_coeff.size());
    for (unsigned int ic = 0; ic < disp_coeff.size(); ++ic) {
        vec_disp_coeff[ic] = disp_coeff[ic];
    }
    for( unsigned int istep = 1; istep < 201; ++istep)
    {
        std::cout << "istep = " << istep << ";"<<std::endl;
        if (istep == 1) {
            assemble_system(true);
        }else{
            assemble_system();
        }
        make_constrains();
        solve();
        vtk_plot("quater_sphere_= "+std::to_string(istep)+".vtu", dof_handler, mapping_collection, vec_values, istep * vec_disp_coeff, vec_pressure_coeff);
    }
}



int main()
{
    const int dim = 2, spacedim = 3;
    Triangulation<dim,spacedim> mesh = set_mesh<dim,spacedim>("quarter_sphere");
    Nonlinear_shell<dim, spacedim> nonlinear_thin_shell(mesh);
    nonlinear_thin_shell.run();
    
    std::cout <<"finished.\n";
    
    return 0;
}
