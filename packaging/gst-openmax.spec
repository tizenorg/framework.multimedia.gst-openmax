Name:       gst-openmax
Summary:    GStreamer plug-in that allows communication with OpenMAX IL components
Version:    0.10.1
Release:    206
Group:      TO_BE/FILLED_IN

License:    LGPLv2.1

ExclusiveArch: %arm
Source: %{name}-%{version}.tar.gz

%description
gst-openmax is a GStreamer plug-in that allows communication with OpenMAX IL components.
Multiple OpenMAX IL implementations can be used.

%prep
%setup -q

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/%{_libdir}
mkdir -p %{buildroot}/%{_libdir}/gstreamer-0.10
%if "%{?tizen_profile_name}"=="wearable"
install -m 644 lib/wearable/libgstomx.so %{buildroot}%{_libdir}/gstreamer-0.10/
%elseif "%{?tizen_profile_name}"=="mobile"
install -m 644 lib/mobile/libgstomx.so %{buildroot}%{_libdir}/gstreamer-0.10/
%endif
mkdir -p %{buildroot}/usr/share/license
cp COPYING %{buildroot}/usr/share/license/%{name}

%post

%postun

%files
%manifest gst-openmax.manifest
%defattr(-,root,root,-)
%{_libdir}/gstreamer-0.10/lib*.so*
/usr/share/license/%{name}

