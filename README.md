## ParaHeat: Parallel and Scalable Heat Methods for Geodesic Distance Computation

This is the source code for the algorithms in the following paper:

* Jiong Tao, Juyong Zhang, Bailin Deng, Zheng Fang, Yue Peng, Ying He. 2019. Parallel and Scalable Heat Methods for Geodesic Distance Computation. [arXiv:1812.06060](https://arxiv.org/abs/1812.06060).


### Compling the code

1. The code implements the following three commands:

	* `GeodDistSolver` for computing geodesic distance;
	* `ViewScalarField` for visualizing the distance on a mesh;
	* `CompareDistance` for computing mean relative error of the computed distance.


2. The code requires [Eigen] (http://http://eigen.tuxfamily.org). 
	* On Ubuntu or Debian, this can be installed using the following command: 
			
			$ apt-get instal libeigen3-dev

	* On macOS, Eigen can be installed using [Homebrew](https://brew.sh/) command:

			$ brew install eigen
  
	* On Windows, you can download Eigen and extract it into the `external` folder. Make sure you rename the Eigen folder as `eigen`, so that you can locate the file `external/eigen/Eigen/Dense`.


3. In addition, compiling `ViewScalarField` requires [libigl](https://libigl.github.io/) and [glfw](https://www.glfw.org/):
	* Download libigl from github, unzip it and rename the folder as `libigl`, and place it inside the `external` folder. Make sure you can locate the folder `external/libigl/include`.
	* To install glfw: 
		* On Ubuntu or Debian, use command `apt-get install libglfw3-dev`. 
		* On macOs, use command `brew install glfw`. 
		* On Windows, download the binary from the homepage and install.


4. After installing all the dependencies, use cmake to generate the project file and compile. Create a folder `build` within the root folder of source code, then run

	```
	$ cmake -DCMAKE_BUILD_TYPE=Release ..
	$ make
	```

	If you use a cmake user interface (using the `ccmake` command for the curses interface, or using a GUI), you can turn on/off OpenMP support and the compilation of the viewer before compling the code.
	
	* The default compiler on macOS (Apple Clang from Xcode) does not support OpenMP. To achieve best performance, it is recommended to compile the code with Homebrew gcc. For example, if you create a build folder in the source code directory, then use the following command (assuming gcc-8 and g++-8 are installed by Homebrew)
		
			$ cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc-8 -DCMAKE_CXX_COMPILER=g++-8 ..



### Usage of commands

1. To compute geodesic distance, use the command

		$ GeodDistSolver PARAMETERS_FILE MESH_FILE DISTANCE_FILE

	* PARAMETERS_FILE: the solver parameter file; an example file (SolverParams.txt) is provided.
	* MESH_FILE: the triangle mesh file.
	* DISTANCE_FILE: an output file that stores the distance for each vertex.

	The command will print out peak memory consumption at the end.



2. To visualize the geodesic distance, use the command
 
		$ ViewScalarField MESH_FILE DATA_FILE

	* MESH_FILE: the triangle mesh file.
	* DATA_FILE: a distance file generated by the command GeodDistSolver.



3. To compute distance error

		$ CompareDistance PARAMETERS_FILE DISTANCE_FILE REFERENCE_DISTANCE_FILE
  
	* PARAMETERS_FILE: the parameter file previously used for computing geodesic distance; the command will read the source vertex array from this file.
	* DISTANCE_FILE: the computed distance file.
	* REFERENCE_DISTANCE_FILE: a reference distance file that stores the ground-truth distance.


### License
The code is released under BSD 3-Clause License.


### Contact

For any comments or questions, please contact Jiong Tao<<taojiong@mail.ustc.edu.cn>>, Bailin Deng <<bldeng@gmail.com>>, or Yue Peng <<echoyue@mail.ustc.edu.cn>>.