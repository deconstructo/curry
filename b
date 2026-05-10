cmake -B build-release -DCMAKE_BUILD_TYPE=Release \                                                             
  -DBUILD_MODULE_CRYPTO=ON \                                                                          
  -DBUILD_MODULE_LDAP=ON \                                                                            
  -DBUILD_MODULE_STORAGE=ON \                                                                         
  -DBUILD_MODULE_GRAPHQL=ON \                                                                         
  -DBUILD_MODULE_IMAGE=ON \                                                                           
  -DBUILD_MODULE_GIT=ON \                                                                             
  -DBUILD_MODULE_PLPLOT=ON \                                                                          
  -DBUILD_MODULE_MQTT=ON \  
  -DBUILD_MODULE_QT6=ON                                                                               
cmake --build build-release -j$(nproc)   
