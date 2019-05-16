make clean
rm data/installFinalizer.3dsx
cd installFinalizer
make clean
make
cp installFinalizer.3dsx ../data/installFinalizer.3dsx
cd ..
make firm SCRIPT_RUNNER=1
rm output/*.sha
rm output/*_dev.*
mv output/GodMode9.firm output/cartInstall.firm
PAUSE