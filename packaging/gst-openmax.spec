Name:       gst-openmax
Summary:    GStreamer plug-in that allows communication with OpenMAX IL components
Version:    0.10.1
%if 0%{?tizen_profile_mobile}
Release:    9
Group:      Application/Multimedia
%else
Release:    202
Group:      TO_BE/FILLED_IN
%endif
License:    LGPLv2.1
Source0:    %{name}-%{version}.tar.gz
BuildRequires: which
BuildRequires: pkgconfig(gstreamer-0.10)
BuildRequires: pkgconfig(gstreamer-plugins-base-0.10)
%if "%{_repository}" == "wearable"
BuildRequires: pkgconfig(iniparser)

BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(libtbm)
BuildRequires:  pkgconfig(libdri2)
BuildRequires:  pkgconfig(xfixes)
%endif

%description
gst-openmax is a GStreamer plug-in that allows communication with OpenMAX IL components.
Multiple OpenMAX IL implementations can be used.

%prep
%setup -q

%build
%if 0%{?tizen_profile_mobile}
cd ./mobile
%else
cd ./wearable
%endif
./autogen.sh --noconfigure
%if 0%{?tizen_profile_mobile}
%configure --disable-static --prefix=/usr
%else
CFLAGS="$CFLAGS -Wno-unused-but-set-variable -Wno-unused-local-typedefs" %configure --disable-static --prefix=%{_prefix}\
export CFLAGS="$CFLAGS -DTIZEN_DEBUG_ENABLE"
export CXXFLAGS="$CXXFLAGS -DTIZEN_DEBUG_ENABLE"
export FFLAGS="$FFLAGS -DTIZEN_DEBUG_ENABLE"
%endif

make %{?jobs:-j%jobs}


%install
%if 0%{?tizen_profile_wearable}
cd ./wearable
%else
cd ./mobile
%endif
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/share/license
cp COPYING %{buildroot}/usr/share/license/%{name}
%make_install

%files
%if 0%{?tizen_profile_mobile}
%manifest ./mobile/gst-openmax.manifest
%else
%manifest ./wearable/gst-openmax.manifest
%defattr(-,root,root,-)
%endif
%{_libdir}/gstreamer-0.10/libgstomx.so
/usr/share/license/%{name}

