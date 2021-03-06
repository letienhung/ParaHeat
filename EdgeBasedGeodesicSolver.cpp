// BSD 3-Clause License
//
// Copyright (c) 2019, Jiong Tao, Bailin Deng, Yue Peng
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "EdgeBasedGeodesicSolver.h"
#include "surface_mesh/IO.h"
#include "OMPHelper.h"
#include <iostream>
#include <utility>
#include <limits>

EdgeBasedGeodesicSolver::EdgeBasedGeodesicSolver()
    : model_scaling_factor(1.0),
      bfs_laplacian_coef(NULL),
      current_SX(NULL),
      prev_SX(NULL),
      need_compute_residual_norms(false),
      n_vertices(0),
      n_faces(0),
      n_edges(0),
      n_halfedges(0),
      n_interior_edges(0),
      iter_num(0),
      primal_residual_sqr_norm(0),
      dual_residual_sqr_norm(0),
      primal_residual_sqr_norm_threshold(0),
      dual_residual_sqr_norm_threshold(0),
      output_progress(false),
      optimization_converge(false),
      optimization_end(false) {
}

const Eigen::VectorXd& EdgeBasedGeodesicSolver::get_distance_values() {
  return geod_dist_values;
}

bool EdgeBasedGeodesicSolver::solve(const char *mesh_file,
                                    const Parameters& para) {
  param = para;

  std::cout << "Reading triangle mesh......" << std::endl;

  if (!load_input(mesh_file)) {
    return false;
  }

  normalize_mesh();

  std::cout << "Initialize BFS path......" << std::endl;

  Timer timer;
  Timer::EventID start = timer.get_time();

  // Precompute breadth-first propagation order
  init_bfs_paths();
  std::cout << "Gauss-Seidel initilization of gradients......" << std::endl;

  Timer::EventID before_GS = timer.get_time();

  gauss_seidel_init_gradients();

  Timer::EventID before_ADMM = timer.get_time();
  std::cout << "ADMM solver for integrable gradients......" << std::endl;

  prepare_integrate_geodesic_distance();

  compute_integrable_gradients();

  Timer::EventID after_ADMM = timer.get_time();
  std::cout << "Recovery of geodesic distance......" << std::endl;

  integrate_geodesic_distance();

  Timer::EventID end = timer.get_time();

  std::cout << std::endl;
  std::cout << "====== Timing ======" << std::endl;
  std::cout << "Pre-computation of BFS paths: "
            << timer.elapsed_time(start, before_GS) << " seconds" << std::endl;
  std::cout << "Gauss-Seidel initialization of gradients: "
            << timer.elapsed_time(before_GS, before_ADMM) << " seconds"
            << std::endl;
  std::cout << "ADMM solver for integrable gradients: "
            << timer.elapsed_time(before_ADMM, after_ADMM) << " seconds"
            << std::endl;
  std::cout << "Integration of gradients: "
            << timer.elapsed_time(after_ADMM, end) << " seconds" << std::endl;
  std::cout << "Total time: " << timer.elapsed_time(start, end) << " seconds"
            << std::endl;

  return true;
}

void EdgeBasedGeodesicSolver::init_bfs_paths() {
  bfs_vertex_list.resize(n_vertices);
  bfs_vertex_list.fill(-1);
  transition_halfedge_idx.setConstant(n_vertices, -1);

  bfs_laplacian_coef_addr.resize(n_vertices + 1);
  bfs_laplacian_coef_addr(0) = 0;

  // Store source vertices as the first layer
  std::vector<bool> visited(n_vertices, false);
  std::vector<int> front1 = param.source_vertices, front2;
  std::vector<int> *current_front = &front1, *next_front = &front2;

  int n_sources = param.source_vertices.size();

  std::vector<int> bfs_segment_addr_vec;
  bfs_segment_addr_vec.push_back(0);
  bfs_segment_addr_vec.push_back(n_sources);

  int id = 0;
  for (; id < n_sources; ++id) {
    int current_source_vtx = param.source_vertices[id];
    visited[current_source_vtx] = true;
    bfs_vertex_list(id) = current_source_vtx;
    bfs_laplacian_coef_addr(id + 1) = bfs_laplacian_coef_addr(id)
        + mesh.valence(MeshType::Vertex(current_source_vtx)) + 1;
  }

  while (!current_front->empty()) {

    next_front->clear();

    for (int k = 0; k < static_cast<int>(current_front->size()); ++k) {
      MeshType::Vertex vh(current_front->at(k));
      MeshType::Halfedge_around_vertex_circulator vhc, vhc_end;
      vhc = vhc_end = mesh.halfedges(vh);

      do {
        MeshType::Halfedge heh = *vhc;
        MeshType::Vertex next_vh = mesh.to_vertex(heh);
        int next_v = next_vh.idx();

        if (!visited[next_v]) {
          next_front->push_back(next_v);
          bfs_vertex_list(id) = next_v;
          // Each segment stores the weights for neighbors and the current vertex for GS update
          bfs_laplacian_coef_addr(id + 1) = bfs_laplacian_coef_addr(id)
              + mesh.valence(next_vh) + 1;
          transition_halfedge_idx(id) = heh.idx();
          id++;
        }

        visited[next_v] = true;

      } while (++vhc != vhc_end);
    }

    bfs_segment_addr_vec.push_back(
        bfs_segment_addr_vec.back() + next_front->size());
    std::swap(current_front, next_front);
  }

  bfs_segment_addr = Eigen::Map < Eigen::VectorXi
      > (bfs_segment_addr_vec.data(), bfs_segment_addr_vec.size());
}

bool EdgeBasedGeodesicSolver::load_input(const char* mesh_file) {
  if (!surface_mesh::read_mesh(mesh, mesh_file)) {
    std::cerr << "Error: unable to read input mesh from the file " << mesh_file
              << std::endl;
    return false;
  }

  mesh.free_memory();  // Free unused memory

  n_vertices = mesh.n_vertices();
  n_faces = mesh.n_faces();
  n_edges = mesh.n_edges();
  n_halfedges = mesh.n_halfedges();

  if (n_vertices == 0 || n_faces == 0 || n_edges == 0) {
    std::cerr << "Error: zero mesh element count " << std::endl;
    return false;
  }

  for (int i = 0; i < static_cast<int>(param.source_vertices.size()); ++i) {
    if (param.source_vertices[i] < 0
        || param.source_vertices[i] >= n_vertices) {
      std::cerr << "Error: invalid source vertex index "
                << param.source_vertices[i] << std::endl;
      return false;
    }
  }

  return true;
}

void EdgeBasedGeodesicSolver::normalize_mesh() {
  std::vector<surface_mesh::Point> &pos = mesh.points();

  surface_mesh::Point min_coord = pos.front(), max_coord = pos.front();
  std::vector<surface_mesh::Point>::iterator iter = pos.begin(), iter_end = pos
      .end();

  for (++iter; iter != iter_end; ++iter) {
    surface_mesh::Point &coord = *iter;
    min_coord.minimize(coord);
    max_coord.maximize(coord);
  }

  model_scaling_factor = surface_mesh::norm(max_coord - min_coord);
  surface_mesh::Point center_pos = (min_coord + max_coord) * 0.5;

  for (iter = pos.begin(); iter != iter_end; ++iter) {
    surface_mesh::Point &coord = *iter;
    coord -= center_pos;
    coord /= model_scaling_factor;
  }
}

void EdgeBasedGeodesicSolver::gauss_seidel_init_gradients() {
  edge_vector.resize(3, n_edges);
  DenseVector edge_sqr_length;
  edge_sqr_length.setZero(n_edges);

  face_area.resize(n_faces);

  int n_halfedges = mesh.n_halfedges();
  DenseVector halfedge_halfcot;  // Collect the half cotan value for edge halfedge, to be used for computing cotan Laplacian weights
  halfedge_halfcot.setZero(n_halfedges);

  double step_length = 0;
  HeatScalar init_source_val = 1;
  VectorHS current_d;
  VectorHS temp_d;
  VectorHS vertex_area;
  int gs_iter = 0;
  int segment_count = 0;
  int n_segments = 0;
  int segment_begin_addr = 0, segment_end_addr = 0;
  bool end_gs_loop = false;
  bool reset_iter = true;
  bool need_check_residual = false;
  HeatScalar eps = 0;
  VectorHS heatflow_residuals;

  OMP_PARALLEL
  {
    // Compute Laplacian weights
    OMP_FOR
    for (int i = 0; i < n_edges; ++i) {
      // Precompute edge vectors and squared edge length,
      // to be used later for computing cotan weights and areas
      MeshType::Halfedge heh = mesh.halfedge(MeshType::Edge(i), 0);
      Eigen::Vector3d edge_vec = to_eigen_vec3d(
          mesh.position(mesh.to_vertex(heh))
              - mesh.position(mesh.from_vertex(heh)));
      double l2 = edge_vec.squaredNorm();
      edge_vector.col(i) = edge_vec;
      edge_sqr_length(i) = l2;
    }

    OMP_SINGLE
    {
      // Compute heat flow step size
      double h = edge_sqr_length.array().sqrt().mean();
      step_length = h * h;
    }

    OMP_FOR
    for (int i = 0; i < n_faces; ++i) {
      // Compute face areas and half-cotan weights for halfedges
      Eigen::Vector3i fh_idx, fe_idx;
      Eigen::Vector3d edge_l2;
      int k = 0;

      MeshType::Halfedge_around_face_circulator fhc, fhc_end;
      fhc = fhc_end = mesh.halfedges(MeshType::Face(i));
      do {
        MeshType::Halfedge heh = *fhc;
        fh_idx(k) = heh.idx();
        fe_idx(k) = mesh.edge(heh).idx();
        edge_l2(k) = edge_sqr_length(fe_idx(k));
        k++;
      } while (++fhc != fhc_end);

      double area = edge_vector.col(fe_idx(0)).cross(edge_vector.col(fe_idx(1)))
          .norm() * 0.5;
      for (int j = 0; j < 3; ++j) {
        halfedge_halfcot(fh_idx(j)) = 0.125
            * (edge_l2((j + 1) % 3) + edge_l2((j + 2) % 3) - edge_l2(j)) / area;
      }

      face_area(i) = area;
    }

    OMP_SINGLE
    {
      // Allocate arrays for storing relevant vertices and weights for Laplacian operator at each vertex
      edge_sqr_length.resize(0);
      int n_laplacian_vertices = n_edges * 2 + n_vertices;
      bfs_laplacian_coef = new std::pair<int, double>[n_laplacian_vertices];
      vertex_area.setZero(n_vertices);
    }

    OMP_FOR
    for (int i = 0; i < n_vertices; ++i) {
      // Compute and store vertex indices and weights for Laplacian operators
      int start_addr = bfs_laplacian_coef_addr(i), end_addr =
          bfs_laplacian_coef_addr(i + 1);

      DenseVector weights;
      IndexVector vtx_idx;
      int n = end_addr - start_addr;
      weights.setZero(n);
      vtx_idx.setZero(n);

      int v_idx = bfs_vertex_list(i);
      MeshType::Vertex vh(v_idx);
      int k = 0;
      MeshType::Halfedge_around_vertex_circulator vhc, vhc_end;
      vhc = vhc_end = mesh.halfedges(vh);

      do {
        MeshType::Halfedge heh = *vhc;
        double w = halfedge_halfcot(heh.idx())
            + halfedge_halfcot(mesh.opposite_halfedge(heh).idx());
        vtx_idx(k) = mesh.to_vertex(heh).idx();
        weights(k) = w;
        k++;
      } while (++vhc != vhc_end);

      vtx_idx(k) = v_idx;
      weights(k) = weights.head(k).sum();  // Store the sum of neighbor weights, to be used for Gauss-Seidel update
      weights *= step_length;

      double A = 0;
      MeshType::Face_around_vertex_circulator vfc, vfc_end;
      vfc = vfc_end = mesh.faces(vh);
      do {
        A += face_area((*vfc).idx());
      } while (++vfc != vfc_end);

      double vertex_A = A / 3.0;
      vertex_area(v_idx) = vertex_A;
      weights(k) += vertex_A;

      for (int j = start_addr; j < end_addr; ++j) {
        bfs_laplacian_coef[j] = std::pair<int, double>(vtx_idx(j - start_addr),
                                                       weights(j - start_addr));
      }
    }

    OMP_SINGLE
    {
      // Set up heat value arrays
      halfedge_halfcot.resize(0);

      int n_sources = param.source_vertices.size();
      HeatScalar total_source_area = 0;
      for (int i = 0; i < n_sources; ++i) {
        total_source_area += vertex_area(param.source_vertices[i]);
      }
      init_source_val = std::sqrt(
          std::min(HeatScalar(n_vertices) / HeatScalar(n_sources),
                   vertex_area.sum() / total_source_area));
      vertex_area.resize(0);

      current_d.setZero(n_vertices);
      for (int i = 0; i < n_sources; ++i) {
        current_d(param.source_vertices[i]) = init_source_val;
      }

      n_segments = bfs_segment_addr.size() - 1;
      int buffer_size = (Eigen::Map < IndexVector
          > (&(bfs_segment_addr[1]), n_segments) - Eigen::Map < IndexVector
          > (&(bfs_segment_addr[0]), n_segments)).maxCoeff();
      temp_d.setZero(buffer_size);

      heatflow_residuals.setZero(n_vertices);
    }

    compute_heatflow_residual(current_d, init_source_val, heatflow_residuals);

    OMP_SINGLE
    {
      // Rescale heat source values to make the initial residual norm close to 1
      HeatScalar init_residual_norm = heatflow_residuals.norm();
      eps = std::max(HeatScalar(1e-16),
                     init_residual_norm * HeatScalar(param.heat_solver_eps));
      std::cout << "Initial residual: " << init_residual_norm << ", threshold: "
                << eps << std::endl;
    }

  }

  while (!end_gs_loop) {
    OMP_PARALLEL
    {
      // Gauss-Seidel update of heat values in breadth-first order
      OMP_SINGLE
      {
        segment_begin_addr = bfs_segment_addr(segment_count);
        segment_end_addr = bfs_segment_addr(segment_count + 1);
      }

      OMP_FOR
      for (int i = segment_begin_addr; i < segment_end_addr; ++i) {
        int lap_coef_begin_addr = bfs_laplacian_coef_addr(i);
        int lap_coef_end_addr = bfs_laplacian_coef_addr(i + 1);

        HeatScalar new_heat_value = 0;
        if (segment_count == 0) {  // Check whether the current vertex is a source
          new_heat_value += init_source_val;
        }

        for (int j = lap_coef_begin_addr; j < lap_coef_end_addr - 1; ++j) {
          std::pair<int, double> &coef = bfs_laplacian_coef[j];
          new_heat_value += current_d(coef.first) * coef.second;
        }

        temp_d(i - segment_begin_addr) = new_heat_value
            / bfs_laplacian_coef[lap_coef_end_addr - 1].second;
      }

      OMP_FOR
      for (int i = segment_begin_addr; i < segment_end_addr; ++i) {
        current_d(bfs_vertex_list(i)) = temp_d(i - segment_begin_addr);
      }

      OMP_SINGLE
      {
        segment_count++;
        reset_iter = (segment_count == n_segments);
        if (reset_iter) {
          gs_iter++;
          segment_count = 0;
        }

        end_gs_loop = gs_iter >= param.heat_solver_max_iter;
        need_check_residual =
            end_gs_loop
                || (reset_iter
                    && (gs_iter % param.heat_solver_convergence_check_frequency
                        == 0));
      }

      if (need_check_residual) {
        compute_heatflow_residual(current_d, init_source_val,
                                  heatflow_residuals);

        OMP_SINGLE
        {
          HeatScalar residual_norm = heatflow_residuals.norm();
          std::cout << "Gauss-Seidel iteration " << gs_iter
                    << ", current residual: " << residual_norm
                    << ", threshold: " << eps << std::endl;

          if (residual_norm <= eps) {
            end_gs_loop = true;
          }
        }
      }
    }
  }

  OMP_PARALLEL
  {
    OMP_SINGLE
    {
      temp_d.resize(0);
      heatflow_residuals.resize(0);
      vertex_area.resize(0);
      delete[] bfs_laplacian_coef;
      bfs_laplacian_coef_addr.resize(0);
      init_grad.resize(3, n_faces);
    }

    // Compute initial gradient and get target edge difference.
    OMP_FOR
    for (int i = 0; i < n_faces; ++i) {
      Matrix3HS edge_vecs;
      Vector3HS heat_vals;
      int k = 0;

      MeshType::Halfedge_around_face_circulator fhc, fhc_end;
      fhc = fhc_end = mesh.halfedges(MeshType::Face(i));

      do {
        MeshType::Halfedge heh = *fhc;
        MeshType::Edge eh = mesh.edge(heh);
        Eigen::Vector3d current_edge = edge_vector.col(eh.idx());
        if (mesh.halfedge(eh, 0) != heh) {
          current_edge *= -1;
        }

        edge_vecs(0, k) = HeatScalar(current_edge[0]);
        edge_vecs(1, k) = HeatScalar(current_edge[1]);
        edge_vecs(2, k) = HeatScalar(current_edge[2]);
        heat_vals(k) = current_d(mesh.to_vertex(heh).idx());
        ++k;

      } while (++fhc != fhc_end);

      heat_vals.normalize();
      edge_vecs.normalize();

      Vector3HS N = edge_vecs.col(0).cross(edge_vecs.col(1)).normalized();
      Vector3HS V = edge_vecs.col(0) * heat_vals(1)
          + edge_vecs.col(1) * heat_vals(2) + edge_vecs.col(2) * heat_vals(0);
      Vector3HS grad_vec = V.cross(N).normalized();
      init_grad(0, i) = grad_vec(0);
      init_grad(1, i) = grad_vec(1);
      init_grad(2, i) = grad_vec(2);
    }
  }
}

void EdgeBasedGeodesicSolver::compute_heatflow_residual(
    const VectorHS &heat_values, HeatScalar init_source_val,
    VectorHS &residuals) {
  OMP_FOR
  for (int i = 0; i < n_vertices; ++i) {
    int lap_coef_begin_addr = bfs_laplacian_coef_addr(i);
    int lap_coef_end_addr = bfs_laplacian_coef_addr(i + 1);

    HeatScalar res = 0;
    if (i < static_cast<int>(param.source_vertices.size())) {  // Check whether the current vertex is a source
      res += init_source_val;
    }

    for (int j = lap_coef_begin_addr; j < lap_coef_end_addr; ++j) {
      std::pair<int, double> &coef = bfs_laplacian_coef[j];
      res += heat_values(coef.first) * coef.second
          * ((j == (lap_coef_end_addr - 1)) ? (-1) : 1);
    }

    residuals(i) = res;
  }
}

void EdgeBasedGeodesicSolver::prepare_integrate_geodesic_distance() {
  transition_from_vtx.setConstant(n_vertices, -1);
  transition_edge_idx.setConstant(n_vertices, -1);

  IndexVector num_rows;
  num_rows.setZero(n_edges);

  Z.resize(3 * n_faces);
  S.resize(3, n_faces);
  Q.resize(3, n_faces);
  edges_Y_index.setConstant(2, n_edges, -1);  // the set of rows in Y associated with each edge.

  OMP_PARALLEL
  {
    // Set up incident relation between edges and faces.
    OMP_SINGLE
    {
      for (int i = 0; i < n_faces; ++i) {
        MeshType::Face f = MeshType::Face(i);
        MeshType::Halfedge_around_face_circulator fhc, fhc_end;
        fhc = fhc_end = mesh.halfedges(f);
        int k = 0;

        do {
          MeshType::Edge e = mesh.edge(*fhc);
          int edge_index = mesh.edge(*fhc).idx();
          Eigen::Vector3d e_vector = to_eigen_vec3d(
              mesh.position(mesh.from_vertex(*fhc))
                  - mesh.position(mesh.to_vertex(*fhc)));
          if (*fhc == mesh.halfedge(e, 0))  // the halfedge with index 0 as orientation halfedge
                                    {
            Q(k, i) = 1;
            Z(3 * i + k) = init_grad.col(i).dot(e_vector);

          } else {
            Q(k, i) = -1;
            Z(3 * i + k) = init_grad.col(i).dot(-e_vector);
          }

          S(k, i) = edge_index;
          edges_Y_index(num_rows(edge_index)++, edge_index) = 3 * i + k;
          k++;

        } while (++fhc != fhc_end);
      }
    }

    // Set up transition vector needed in recovering distance step.
    OMP_FOR
    for (int i = 0; i < n_vertices; ++i) {
      MeshType::Halfedge heh(transition_halfedge_idx(i));
      if (heh.is_valid()) {
        MeshType::Vertex from_vh = mesh.from_vertex(heh);
        MeshType::Edge e = mesh.edge(heh);
        transition_from_vtx(i) = from_vh.idx();

        if (heh == mesh.halfedge(e, 0)) {
          transition_edge_idx(i) = -e.idx() - 1;
        } else {
          transition_edge_idx(i) = e.idx();
        }
      }
    }

    OMP_SINGLE
    {
      mesh.clear();
      transition_halfedge_idx.resize(0);
      init_grad.resize(3, 0);

      D.setZero(3 * n_faces);
      X.resize(n_edges);
      Y.setZero(3 * n_faces);

      SX1.setZero(3 * n_faces);
      SX2.setZero(3 * n_faces);
      current_SX = &SX1;
      prev_SX = &SX2;

      primal_residual_sqr_norm_threshold = param.grad_solver_eps
          * param.grad_solver_eps;
      dual_residual_sqr_norm_threshold = param.grad_solver_eps
          * param.grad_solver_eps;
      (*prev_SX) = (*current_SX);
    }

    // Initialize X.
    OMP_FOR
    for (int i = 0; i < n_edges; ++i) {
      int n_var = 0;
      double r = 0;
      for (int j = 0; j < 2; ++j) {
        int index = edges_Y_index(j, i);
        if (index >= 0) {
          r += Z(index);
          n_var++;
        }
      }

      X(i) = r / n_var;
    }

    // Initialize SX.
    OMP_FOR
    for (int i = 0; i < n_faces; ++i) {
      (*prev_SX)(3 * i) = X(S(0, i));
      (*prev_SX)(3 * i + 1) = X(S(1, i));
      (*prev_SX)(3 * i + 2) = X(S(2, i));
    }
  }
}

void EdgeBasedGeodesicSolver::compute_integrable_gradients() {
  optimization_end = false;
  iter_num = 0;
  OMP_PARALLEL
  {
    while (!optimization_end) {
      update_Y();
      update_X();
      update_dual_variables();
    }
  }
}

void EdgeBasedGeodesicSolver::integrate_geodesic_distance() {
  geod_dist_values.setZero(n_vertices);
  int n_segments = bfs_segment_addr.size() - 1;
  int segment_count = 1;  // We update the distance values strting from the second layer of BFS vertex list
  int segment_begin_addr, segment_end_addr;
  bool end_propagation = false;

  OMP_PARALLEL
  {
    while (!end_propagation) {
      OMP_SINGLE
      {
        segment_begin_addr = bfs_segment_addr(segment_count);
        segment_end_addr = bfs_segment_addr(segment_count + 1);
      }

      OMP_FOR
      for (int i = segment_begin_addr; i < segment_end_addr; ++i) {
        double from_d = geod_dist_values(transition_from_vtx(i));
        int edge_index = transition_edge_idx(i);
        if (edge_index >= 0) {
          geod_dist_values(bfs_vertex_list(i)) = from_d + X(edge_index);
        } else {
          geod_dist_values(bfs_vertex_list(i)) = from_d - X(-(edge_index + 1));
        }
      }

      OMP_SINGLE
      {
        segment_count++;
        end_propagation = (segment_count >= n_segments);
      }
    }
  }

  // Recover geodesic distance in the original scale
  geod_dist_values *= model_scaling_factor;
}

void EdgeBasedGeodesicSolver::update_Y() {
  OMP_FOR
  for (int i = 0; i < n_faces; ++i) {
    Eigen::Vector3d y = prev_SX->segment(3 * i, 3) - D.segment(3 * i, 3);
    Eigen::Vector3d q = Q.col(i).cast<double>();
    Y.segment(3 * i, 3) = y - 1.0 / 3 * q.dot(y) * q;
  }
}

void EdgeBasedGeodesicSolver::update_X() {
  OMP_FOR
  for (int i = 0; i < n_edges; ++i) {
    int n_aux_var = 0;
    double r = 0;
    for (int j = 0; j < 2; ++j) {
      int index = edges_Y_index(j, i);
      if (index >= 0) {
        r += param.penalty * (Y(index) + D(index)) + Z(index);
        n_aux_var++;
      }
    }

    X(i) = r / ((param.penalty + 1) * n_aux_var);
  }
}

void EdgeBasedGeodesicSolver::update_dual_variables() {
  OMP_FOR
  for (int i = 0; i < n_faces; ++i) {
    (*current_SX)(3 * i) = X(S(0, i));
    (*current_SX)(3 * i + 1) = X(S(1, i));
    (*current_SX)(3 * i + 2) = X(S(2, i));
  }

  OMP_SINGLE
  {
    need_compute_residual_norms = ((iter_num + 1)
        % param.grad_solver_convergence_check_frequency == 0);
  }

  OMP_SECTIONS
  {
    OMP_SECTION
    {
      if (need_compute_residual_norms) {
        primal_residual_sqr_norm = (Y - (*current_SX)).squaredNorm();
      }
    }

    OMP_SECTION
    {
      if (need_compute_residual_norms) {
        dual_residual_sqr_norm = ((*current_SX) - (*prev_SX)).squaredNorm()
            * param.penalty * param.penalty;
      }
    }

    OMP_SECTION
    {
      D += Y - (*current_SX);
    }
  }

  OMP_SINGLE
  {
    iter_num++;
    optimization_converge = need_compute_residual_norms
        && (primal_residual_sqr_norm <= primal_residual_sqr_norm_threshold
            && dual_residual_sqr_norm <= dual_residual_sqr_norm_threshold);
    optimization_end = optimization_converge
        || iter_num >= param.grad_solver_max_iter;
    output_progress = need_compute_residual_norms
        && (iter_num % param.grad_solver_output_frequency == 0);

    if (optimization_converge) {
      std::cout << "Solver converged." << std::endl;
    } else if (optimization_end) {
      std::cout << "Maximum number of iterations reached." << std::endl;
    }

    if (output_progress || optimization_end) {
      std::cout << "Iteration " << iter_num << ":" << std::endl;
      std::cout << "Primal residual squared norm: " << primal_residual_sqr_norm
                << ",  threshold:" << primal_residual_sqr_norm_threshold
                << std::endl;
      std::cout << "Dual residual squared norm: " << dual_residual_sqr_norm
                << ",  threshold:" << dual_residual_sqr_norm_threshold
                << std::endl;
    }

    std::swap(current_SX, prev_SX);
  }
}
