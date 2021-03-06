// The libMesh Finite Element Library.
// Copyright (C) 2002-2014 Benjamin S. Kirk, John W. Peterson, Roy H. Stogner

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// C++ includes
#include <fstream>
#include <sstream>
#include <map>

// Local includes
#include "libmesh/elem.h"
#include "libmesh/libmesh_logging.h"
#include "libmesh/mesh_base.h"
#include "libmesh/gnuplot_io.h"

namespace libMesh
{

GnuPlotIO::GnuPlotIO(const MeshBase& mesh_in,
                     const std::string& title,
                     int mesh_properties)
  :
  MeshOutput<MeshBase> (mesh_in),
  _title(title)
{
  _grid       = (mesh_properties & GRID_ON);
  _png_output = (mesh_properties & PNG_OUTPUT);
}

void GnuPlotIO::write(const std::string& fname)
{
  this->write_solution(fname);
}

void GnuPlotIO::write_nodal_data (const std::string& fname,
                                  const std::vector<Number>& soln,
                                  const std::vector<std::string>& names)
{
  START_LOG("write_nodal_data()", "GnuPlotIO");

  this->write_solution(fname, &soln, &names);

  STOP_LOG("write_nodal_data()", "GnuPlotIO");
}




void GnuPlotIO::write_solution(const std::string& fname,
                               const std::vector<Number>* soln,
                               const std::vector<std::string>* names)
{
  // Even when writing on a serialized ParallelMesh, we expect
  // non-proc-0 help with calls like n_active_elem
  // libmesh_assert_equal_to (this->mesh().processor_id(), 0);

  const MeshBase& the_mesh = MeshOutput<MeshBase>::mesh();

  dof_id_type n_active_elem = the_mesh.n_active_elem();

  if (this->mesh().processor_id() == 0)
    {
      std::stringstream data_stream_name;
      data_stream_name << fname << "_data";
      const std::string data_file_name = data_stream_name.str();

      // This class is designed only for use with 1D meshes
      libmesh_assert_equal_to (the_mesh.mesh_dimension(), 1);

      // Make sure we have a solution to plot
      libmesh_assert ((names != NULL) && (soln != NULL));

      // Create an output stream for script file
      std::ofstream out_stream(fname.c_str());

      // Make sure it opened correctly
      if (!out_stream.good())
        libmesh_file_error(fname.c_str());

      // The number of variables in the equation system
      const unsigned int n_vars =
        libmesh_cast_int<unsigned int>(names->size());

      // Write header to stream
      out_stream << "# This file was generated by gnuplot_io.C\n"
                 << "# Stores 1D solution data in GNUplot format\n"
                 << "# Execute this by loading gnuplot and typing "
                 << "\"call '" << fname << "'\"\n"
                 << "reset\n"
                 << "set title \"" << _title << "\"\n"
                 << "set xlabel \"x\"\n"
                 << "set xtics nomirror\n";

      // Loop over the elements to find the minimum and maximum x values,
      // and also to find the element boundaries to write out as xtics
      // if requested.
      Real x_min=0., x_max=0.;

      // construct string for xtic positions at element edges
      std::stringstream xtics_stream;

      MeshBase::const_element_iterator it = the_mesh.active_elements_begin();
      const MeshBase::const_element_iterator end_it =
        the_mesh.active_elements_end();

      unsigned int count = 0;

      for( ; it != end_it; ++it)
        {
          const Elem* el = *it;

          // if el is the left edge of the mesh, print its left node position
          if(el->neighbor(0) == NULL)
            {
              x_min = (*(el->get_node(0)))(0);
              xtics_stream << "\"\" " << x_min << ", \\\n";
            }
          if(el->neighbor(1) == NULL)
            {
              x_max = (*(el->get_node(1)))(0);
            }
          xtics_stream << "\"\" " << (*(el->get_node(1)))(0);

          if(count+1 != n_active_elem)
            {
              xtics_stream << ", \\\n";
            }
          count++;
        }

      out_stream << "set xrange [" << x_min << ":" << x_max << "]\n";

      if(_grid)
        out_stream << "set x2tics (" << xtics_stream.str() << ")\nset grid noxtics noytics x2tics\n";

      if(_png_output)
        {
          out_stream << "set terminal png\n";
          out_stream << "set output \"" << fname << ".png\"\n";
        }

      out_stream << "plot "
                 << axes_limits
                 << " \"" << data_file_name << "\" using 1:2 title \"" << (*names)[0]
                 << "\" with lines";
      if(n_vars > 1)
        {
          for(unsigned int i=1; i<n_vars; i++)
            {
              out_stream << ", \\\n\"" << data_file_name << "\" using 1:" << i+2
                         << " title \"" << (*names)[i] << "\" with lines";
            }
        }

      out_stream.close();


      // Create an output stream for data file
      std::ofstream data(data_file_name.c_str());

      if (!data.good())
        {
          libMesh::err << "ERROR: opening output data file " << std::endl;
          libmesh_error();
        }

      // get ordered nodal data using a map
      typedef std::pair<Real, std::vector<Number> > key_value_pair;
      typedef std::map<Real, std::vector<Number> > map_type;
      typedef map_type::iterator map_iterator;

      map_type node_map;


      it  = the_mesh.active_elements_begin();

      for ( ; it != end_it; ++it)
        {
          const Elem* elem = *it;

          for(unsigned int i=0; i<elem->n_nodes(); i++)
            {
              std::vector<Number> values;

              // Get the global id of the node
              dof_id_type global_id = elem->node(i);

              for(unsigned int c=0; c<n_vars; c++)
                {
                  values.push_back( (*soln)[global_id*n_vars + c] );
                }

              node_map[ the_mesh.point(global_id)(0) ] = values;
            }
        }


      map_iterator map_it = node_map.begin();
      const map_iterator end_map_it = node_map.end();

      for( ; map_it != end_map_it; ++map_it)
        {
          key_value_pair kvp = *map_it;
          std::vector<Number> values = kvp.second;

          data << kvp.first << "\t";

          for(unsigned int i=0; i<values.size(); i++)
            {
              data << values[i] << "\t";
            }

          data << "\n";
        }

      data.close();
    }
}

} // namespace libMesh
