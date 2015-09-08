Name:       gst-openmax
Summary:    GStreamer plug-in that allows communication with OpenMAX IL components
Version:    0.10.1
Release:    4
Group:      TO_BE/FILLED_IN

License:    LGPL-2.1+

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
%if "%{?tizen_target_name}" == "B3"
install -m 644 lib/wearable/B3/libgstomx.so %{buildroot}%{_libdir}/gstreamer-0.10/
%else
install -m 644 lib/wearable/B2/libgstomx.so %{buildroot}%{_libdir}/gstreamer-0.10/
%endif
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

