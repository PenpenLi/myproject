@echo off
:begin

set /p PROJECTDIR=�����빤������Ŀ¼ 

::echo qh360,uc,alipay,nd91
::set /P PLUGINS=�����빤�̲��,�������ö��ŷָ�
echo PROJECTDIR  =  %PROJECTDIR% 
echo PLUGINS     =  %PLUGINS%
echo ��ʼ���𹤳�
pushd .
::��������Ƿ���ȷ
:inputdir
cd %PROJECTDIR%
if not exist jni echo ����Ŀ¼�������������� & set /p PROJECTDIR=�����빤������Ŀ¼  &goto inputdir
REM :inputplugin
REM for %%i in (%PLUGINS%) do if %%i neq qh360 (echo %%i)else if %%i neq uc (echo %%i)else if %%i neq alipay (echo %%i)else if  %%i neq nd91 ()else  (echo ���������� &&set /P PLUGINS=�����빤�̲��,�������ö��ŷָ�goto inputplugin)
::�޸�main.cpp

for /d  %%i in (.\jni\*) do for /f "tokens=*" %%j in (%%i\main.cpp) do (
	echo %%j >> %%i\main1.cpp
	if "%%j" == "#include <android/log.h>"              (
		echo #include "PluginJniHelper.h" >>%%i\main1.cpp
	)else if "%%j" == "PluginJniHelper::setJavaVM(vm);" (
		echo PluginJniHelper::setJavaVM^(vm^); >>%%i\main1.cpp
	)
	if "%%j" == '#include "PluginJniHelper.h"' (
			del %%i\main1.cpp
	)
)
pause