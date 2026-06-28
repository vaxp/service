#!/bin/bash
# =============================================================
# vaxp-setup.sh — سكربت التثبيت التلقائي لمدير جلسات VAXP-OS
# يُثبّت خدمة ديناميكية تكتشف اليوزر عند كل إقلاع تلقائياً
# =============================================================

set -e

SCRIPT_DIR="$(dirname "$0")"
SERVICE_TEMPLATE_SRC="$SCRIPT_DIR/vaxp-session@.service"
LAUNCHER_SERVICE_SRC="$SCRIPT_DIR/vaxp-launch.service"
BINARY_DST="/usr/local/bin/vaxp-session"

SERVICE_TEMPLATE_DST="/etc/systemd/system/vaxp-session@.service"
LAUNCHER_SERVICE_DST="/etc/systemd/system/vaxp-launch.service"

# ─── التحقق من الصلاحيات ───────────────────────────────────────
if [[ "$EUID" -ne 0 ]]; then
    echo "❌ يجب تشغيل السكربت كـ root: sudo $0"
    exit 1
fi

# ─── بناء البرنامج (Compilation) ────────────────────────────────
echo "🛠️ بناء مدير الجلسات (Compilation)..."
cd "$SCRIPT_DIR"
make

# ─── نسخ ملفات النظام ────────────────────────────────────────
echo "📋 نسخ vaxp-session@.service إلى $SERVICE_TEMPLATE_DST ..."
cp "$SERVICE_TEMPLATE_SRC" "$SERVICE_TEMPLATE_DST"

echo "📋 نسخ vaxp-launch.service إلى $LAUNCHER_SERVICE_DST ..."
cp "$LAUNCHER_SERVICE_SRC" "$LAUNCHER_SERVICE_DST"

echo "📋 نسخ vaxp-session.target إلى /usr/lib/systemd/user/ ..."
mkdir -p /usr/lib/systemd/user/
cp "$SCRIPT_DIR/vaxp-session.target" "/usr/lib/systemd/user/vaxp-session.target"

echo "📋 تثبيت البرنامج التنفيذي $BINARY_DST ..."
cp "$SCRIPT_DIR/vaxp-session" "$BINARY_DST"
chmod +x "$BINARY_DST"

# ─── إنشاء ملف أمر vaxp-switch للإشارة للبرنامج ────────────────
# يمكن للمستخدم تشغيل vaxp-switch مباشرة بدلاً من vaxp-session switch
echo '#!/bin/bash' > /usr/local/bin/vaxp-switch
echo 'exec /usr/local/bin/vaxp-session switch "$@"' >> /usr/local/bin/vaxp-switch
chmod +x /usr/local/bin/vaxp-switch

# ─── إعادة تحميل systemd ─────────────────────────────────────────
echo "🔄 إعادة تحميل systemd ..."
systemctl daemon-reload

# ─── تعطيل الخدمات القديمة إن وجدت ─────────────────────────────
for OLD in aether.service aether@*.service aether-launch.service vaxp-session@*.service; do
    if systemctl is-enabled "$OLD" &>/dev/null; then
        echo "🗑️  تعطيل الخدمة القديمة: $OLD ..."
        systemctl disable "$OLD" 2>/dev/null || true
    fi
done

# ─── تفعيل خدمة المُطلِق الديناميكي ────────────────────────────
echo "⚡ تفعيل vaxp-launch.service ..."
systemctl enable vaxp-launch.service

echo ""
echo "══════════════════════════════════════════"
echo "✅ تم التثبيت بنجاح (نسخة C السريعة)!"
echo ""
echo "   الآلية: عند كل إقلاع، سيكتشف vaxp-launch.service"
echo "           اليوزر البشري أو الافتراضي تلقائياً."
echo "           يتم قراءة الجلسة المفضلة لكل مستخدم عبر البرنامج المدمج."
echo ""
echo "   لتشغيله الآن:  sudo systemctl start vaxp-launch.service"
echo "   للتبديل السريع: vaxp-switch --help"
echo "══════════════════════════════════════════"
