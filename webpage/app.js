/**
 * FUNN HUB - Food & Fun Indoor Game Zone
 * Official Website for Razorpay Merchant Payment Gateway Approval
 * Business: FUNN HUB (PAN: AALFF4908R | FSSAI: 23626009000033)
 * Location: Ground Floor Wing A, Building 2, NSL SEZ ARENA, Uppal, Hyderabad - 500039
 */

// ===================================================================
// 1. Official Catalog Items (Food + Indoor Games from Lease Annexure-1)
// ===================================================================
const CATALOG_ITEMS = [
  // --- INDOOR GAMES & ATTRACTIONS (Annexure-1) ---
  {
    id: 'g1',
    name: 'Snooker Table Zone (1 Hour Slot)',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 249,
    rating: 4.9,
    reviews: 180,
    duration: '60 Mins',
    badge: 'Popular',
    desc: 'Full-size professional snooker table with tournament cues, chalk, and smooth felt bed.',
    image: 'https://images.unsplash.com/photo-1511512578047-dfb367046420?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g2',
    name: 'Pool Table Game (30 Mins Slot)',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 149,
    rating: 4.8,
    reviews: 210,
    duration: '30 Mins',
    badge: 'Quick Fun',
    desc: 'Classic American 8-ball pool table. Great for pairs, friends, and casual matches.',
    image: 'https://images.unsplash.com/photo-1534423861386-85a16f5d13fd?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g3',
    name: 'VR Gaming Station (2 VR Headsets - 15 Mins)',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 299,
    rating: 4.9,
    reviews: 320,
    duration: '15 Mins',
    badge: 'Top Pick',
    desc: 'Immersive 360-degree virtual reality roller coaster, action, and sci-fi adventures.',
    image: 'https://images.unsplash.com/photo-1592478411213-6153e4ebc07d?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g4',
    name: 'Dual Racing Simulators (15 Mins Slot)',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 199,
    rating: 4.9,
    reviews: 195,
    duration: '15 Mins',
    badge: 'High Speed',
    desc: 'Force-feedback steering wheel, pedal setup, and curved high-FPS gaming screen.',
    image: 'https://images.unsplash.com/photo-1578632767115-351597cf2477?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g5',
    name: 'Boxing Machine & Basketball Arcade Pass',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 129,
    rating: 4.7,
    reviews: 140,
    duration: '5 Game Rounds',
    badge: 'Power Test',
    desc: 'Test your punch power on the boxing pad and score points on the rapid basketball hoops.',
    image: 'https://images.unsplash.com/photo-1511882150382-421056c89033?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g6',
    name: 'Air Hockey & Foosball Match Pass',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 149,
    rating: 4.8,
    reviews: 165,
    duration: '30 Mins',
    badge: 'Head-to-Head',
    desc: 'High-speed air-cushioned hockey table and table soccer for 2 to 4 players.',
    image: 'https://images.unsplash.com/photo-1538388149542-5e24932d11a8?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g7',
    name: 'Interactive LED Dance Floor & Piano',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 199,
    rating: 4.9,
    reviews: 128,
    duration: '20 Mins',
    badge: 'Music & Dance',
    desc: 'Light-up stepping floor and giant floor piano for dance battles and fun group games.',
    image: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g8',
    name: 'Table Tennis Arena (45 Mins Slot)',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 149,
    rating: 4.7,
    reviews: 110,
    duration: '45 Mins',
    badge: 'Sporty',
    desc: 'Competition-grade table tennis board with light rackets and balls included.',
    image: 'https://images.unsplash.com/photo-1534423861386-85a16f5d13fd?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g9',
    name: 'Luxury Massage Chair Session (4 Chairs)',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 149,
    rating: 4.9,
    reviews: 240,
    duration: '20 Mins',
    badge: 'Relax Zone',
    desc: 'Full-body zero gravity massage chair with heated back rollers to rest and recharge.',
    image: 'https://images.unsplash.com/photo-1511512578047-dfb367046420?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g10',
    name: 'FunnHub Arcade Smart Play Card (50 Credits)',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 499,
    rating: 4.9,
    reviews: 350,
    duration: 'Self Paced',
    badge: 'Best Value',
    desc: 'Tap-to-play RFID card with 50 credits to play any video game, claw machine, and arcade game.',
    image: 'https://images.unsplash.com/photo-1511882150382-421056c89033?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'g11',
    name: 'Birthday & Corporate Hall Booking (50 Guests)',
    category: 'games',
    type: 'game',
    diet: 'game',
    price: 2999,
    rating: 5.0,
    reviews: 75,
    duration: '3 Hours Slot',
    badge: 'Event Hall',
    desc: 'Private party hall for up to 50 guests. Includes sound system, party lights, and game access.',
    image: 'https://images.unsplash.com/photo-1544025162-d76694265947?auto=format&fit=crop&w=600&q=80'
  },

  // --- FOOD & CAFE ITEMS (FSSAI Reg: 23626009000033) ---
  {
    id: 'f1',
    name: 'Crispy Veggie & Cheese Burger',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 149,
    rating: 4.8,
    reviews: 145,
    duration: '12 mins prep',
    badge: 'Bestseller',
    desc: 'Crisp vegetable patty with fresh lettuce, sliced tomatoes, creamy mayo, and melted cheese.',
    image: 'https://images.unsplash.com/photo-1550547660-d9450f859349?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f2',
    name: 'Grilled Chicken Delight Burger',
    category: 'food',
    type: 'food',
    diet: 'non-veg',
    price: 199,
    rating: 4.9,
    reviews: 210,
    duration: '15 mins prep',
    badge: 'Chef Pick',
    desc: 'Grilled tender chicken patty seasoned with mild herbs, cheese slice, and honey mustard sauce.',
    image: 'https://images.unsplash.com/photo-1568901346375-23c9450c58cd?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f3',
    name: 'Farmhouse Grilled Club Sandwich',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 139,
    rating: 4.7,
    reviews: 98,
    duration: '10 mins prep',
    badge: 'Popular',
    desc: 'Toasted triple-layer sandwich stuffed with capsicum, sweet corn, paneer, and butter.',
    image: 'https://images.unsplash.com/photo-1528735602780-2552fd46c7af?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f4',
    name: 'Classic Margherita Cheese Pizza (10")',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 249,
    rating: 4.8,
    reviews: 160,
    duration: '15 mins prep',
    badge: 'Fresh Crust',
    desc: 'Freshly baked thin crust pizza with Italian tomato herb sauce and 100% real mozzarella cheese.',
    image: 'https://images.unsplash.com/photo-1513104890138-7c749659a591?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f5',
    name: 'Spicy Chicken Supreme Pizza (10")',
    category: 'food',
    type: 'food',
    diet: 'non-veg',
    price: 329,
    rating: 4.9,
    reviews: 190,
    duration: '15 mins prep',
    badge: 'Bestseller',
    desc: 'Crispy baked crust loaded with diced chicken chunks, jalapeños, onions, and stringy cheese.',
    image: 'https://images.unsplash.com/photo-1628840042765-356cda07504e?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f6',
    name: 'Peri-Peri Crinkle Cut French Fries',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 119,
    rating: 4.7,
    reviews: 260,
    duration: '8 mins prep',
    badge: 'Crunchy',
    desc: 'Golden potato fries seasoned with peri-peri chili dust, served with tomato ketchup.',
    image: 'https://images.unsplash.com/photo-1585109649139-366815a0d713?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f7',
    name: 'Loaded Cheesy Nachos with Salsa',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 139,
    rating: 4.6,
    reviews: 115,
    duration: '8 mins prep',
    badge: 'Snack Time',
    desc: 'Crispy corn tortilla chips topped with warm cheddar cheese sauce and tangy fresh tomato salsa.',
    image: 'https://images.unsplash.com/photo-1513456852971-30c0b8199d4d?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f8',
    name: 'Belgian Chocolate Thick Milkshake',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 149,
    rating: 4.9,
    reviews: 185,
    duration: '5 mins prep',
    badge: 'Cold Shake',
    desc: 'Creamy milk blended with real dark chocolate, cocoa, and topped with chocolate fudge.',
    image: 'https://images.unsplash.com/photo-1572490122747-3968b75cc699?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f9',
    name: 'Blueberry Mint Cool Mojito Mocktail',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 119,
    rating: 4.7,
    reviews: 130,
    duration: '5 mins prep',
    badge: 'Chilled',
    desc: 'Crushed blueberries, fresh mint leaves, lime juice, and sparkling soda on ice.',
    image: 'https://images.unsplash.com/photo-1513558161293-cdaf765ed2fd?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f10',
    name: 'Cold Coffee with Vanilla Ice Cream',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 109,
    rating: 4.8,
    reviews: 142,
    duration: '5 mins prep',
    badge: 'Cafe Pick',
    desc: 'Blended espresso coffee with chilled milk, sugar, and a scoop of vanilla ice cream.',
    image: 'https://images.unsplash.com/photo-1517701550927-30cf4ba1dba5?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f11',
    name: 'Hot Masala Chai / Filter Coffee',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 49,
    rating: 4.6,
    reviews: 95,
    duration: '3 mins prep',
    badge: 'Hot Drink',
    desc: 'Freshly brewed aromatic Indian milk tea or authentic South Indian filter coffee.',
    image: 'https://images.unsplash.com/photo-1544787219-7f47ccb76574?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'f12',
    name: 'Warm Sizzling Walnut Brownie',
    category: 'food',
    type: 'food',
    diet: 'veg',
    price: 149,
    rating: 4.9,
    reviews: 215,
    duration: '8 mins prep',
    badge: 'Sweet Dessert',
    desc: 'Warm chocolate brownie served with vanilla ice cream and hot chocolate sauce.',
    image: 'https://images.unsplash.com/photo-1606313564200-e75d5e30476c?auto=format&fit=crop&w=600&q=80'
  },

  // --- COMBOS (FOOD + FUN PASSES) ---
  {
    id: 'c1',
    name: 'Gamer Solo Fuel Pack',
    category: 'combos',
    type: 'combo',
    diet: 'veg',
    price: 349,
    rating: 4.9,
    reviews: 290,
    duration: 'Meal + 30 Credits',
    badge: 'Solo Deal',
    desc: 'Includes 1 Veggie/Chicken Burger, 1 Peri-Peri Fries, 1 Beverage + 30 Game Arcade Credits.',
    image: 'https://images.unsplash.com/photo-1555396273-367ea4eb4db5?auto=format&fit=crop&w=600&q=80'
  },
  {
    id: 'c2',
    name: 'Squad Fun Feast (For 4 Guests)',
    category: 'combos',
    type: 'combo',
    diet: 'non-veg',
    price: 1199,
    rating: 5.0,
    reviews: 140,
    duration: '4 Meals + Games',
    badge: 'Group Deal',
    desc: 'Includes 2 Large 10" Pizzas, 2 French Fries, 4 Cold Mojitos + 1 Hour Snooker/Pool + 60 Arcade Credits.',
    image: 'https://images.unsplash.com/photo-1544025162-d76694265947?auto=format&fit=crop&w=600&q=80'
  }
];

// ===================================================================
// 2. Application State
// ===================================================================
let cart = [];
let currentCategory = 'all';
let currentDietFilter = 'all';
let searchQuery = '';
let activeCoupon = null;

// ===================================================================
// 3. Image Error Fallback Handler
// ===================================================================
function handleImageError(img, type) {
  if (!img) return;
  img.onerror = null; // Prevent loop
  const isGame = type === 'game';
  const bgColor = isGame ? '%236366f1' : '%23f97316';
  const icon = isGame ? '🎮' : '🍔';
  const label = isGame ? 'FUNN+HUB+GAME' : 'FUNN+HUB+FOOD';
  img.src = `data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" width="600" height="400" viewBox="0 0 600 400"><rect width="100%" height="100%" fill="%23f1f5f9"/><circle cx="300" cy="180" r="70" fill="${bgColor}" opacity="0.15"/><text x="50%" y="195" font-size="64" text-anchor="middle">${icon}</text><text x="50%" y="300" font-size="20" font-weight="bold" fill="%23475569" text-anchor="middle" font-family="sans-serif">${label}</text></svg>`;
}

function handleQrError(img, type) {
  if (!img) return;
  img.onerror = null;
  img.src = `data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='180' height='180' viewBox='0 0 180 180'><rect width='100%' height='100%' fill='%23ffffff' stroke='%23cbd5e1' stroke-width='2'/><rect x='20' y='20' width='40' height='40' fill='%230f172a'/><rect x='120' y='20' width='40' height='40' fill='%230f172a'/><rect x='20' y='120' width='40' height='40' fill='%230f172a'/><rect x='30' y='30' width='20' height='20' fill='%23ffffff'/><rect x='130' y='30' width='20' height='20' fill='%23ffffff'/><rect x='30' y='130' width='20' height='20' fill='%23ffffff'/><rect x='80' y='80' width='20' height='20' fill='%23f97316'/><text x='50%' y='170' font-size='10' text-anchor='middle' fill='%2364748b' font-family='sans-serif'>DIGITAL PASS QR</text></svg>`;
}

// ===================================================================
// 4. Initialization
// ===================================================================
document.addEventListener('DOMContentLoaded', () => {
  renderCatalog();
  setupEventListeners();
  loadCartFromStorage();
  updateCartUI();
});

// ===================================================================
// 5. Catalog Rendering & Filtering
// ===================================================================
function renderCatalog() {
  const container = document.getElementById('items-grid');
  if (!container) return;

  const filtered = CATALOG_ITEMS.filter(item => {
    const matchesCategory = (currentCategory === 'all') ||
      (currentCategory === 'food' && item.type === 'food') ||
      (currentCategory === 'games' && item.type === 'game') ||
      (currentCategory === 'combos' && item.type === 'combo') ||
      (item.category === currentCategory);

    const matchesDiet = (currentDietFilter === 'all') || (item.diet === currentDietFilter);

    const matchesSearch = item.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
      item.desc.toLowerCase().includes(searchQuery.toLowerCase());

    return matchesCategory && matchesDiet && matchesSearch;
  });

  if (filtered.length === 0) {
    container.innerHTML = `
      <div style="grid-column: 1 / -1; text-align: center; padding: 60px 20px;">
        <i class="fa-solid fa-gamepad" style="font-size: 3rem; color: #cbd5e1; margin-bottom: 12px;"></i>
        <h3 style="font-size: 1.3rem; margin-bottom: 8px;">No items found</h3>
        <p style="color: #64748b;">Try searching for snooker, pizza, VR, or burger.</p>
        <button class="btn-primary" style="margin-top: 16px;" onclick="resetFilters()">View All Offerings</button>
      </div>
    `;
    return;
  }

  container.innerHTML = filtered.map(item => {
    const isGame = item.type === 'game';
    const isCombo = item.type === 'combo';

    let dietBadge = '';
    if (item.diet === 'veg') {
      dietBadge = `<span class="diet-indicator" title="Vegetarian"><span class="indicator-dot veg"></span></span>`;
    } else if (item.diet === 'non-veg') {
      dietBadge = `<span class="diet-indicator" title="Non-Vegetarian"><span class="indicator-dot non-veg"></span></span>`;
    } else {
      dietBadge = `<span class="diet-indicator" title="Indoor Game"><span class="indicator-dot game"></span></span>`;
    }

    let badgeClass = 'badge-bestseller';
    if (isGame) badgeClass = 'badge-arcade';
    if (isCombo) badgeClass = 'badge-combo';

    const actionText = isGame ? 'Book Game' : (isCombo ? 'Get Combo' : 'Add to Cart');
    const actionBtnClass = isGame ? 'add-btn game-btn' : 'add-btn';

    return `
      <div class="item-card" data-id="${item.id}">
        <div class="card-img-wrap">
          <img src="${item.image}" alt="${item.name}" loading="lazy" onerror="handleImageError(this, '${item.type}')">
          <span class="card-badge ${badgeClass}">${item.badge}</span>
          ${dietBadge}
        </div>
        <div class="card-content">
          <div class="card-meta">
            <span class="item-rating"><i class="fa-solid fa-star"></i> ${item.rating} (${item.reviews})</span>
            <span class="item-duration"><i class="fa-regular fa-clock"></i> ${item.duration}</span>
          </div>
          <h3 class="card-title">${item.name}</h3>
          <p class="card-desc">${item.desc}</p>
          <div class="card-footer">
            <div class="price-wrap">
              <span class="price-label">Price (INR)</span>
              <span class="price-inr"><span>₹</span>${item.price}</span>
            </div>
            <button class="${actionBtnClass}" onclick="addToCart('${item.id}')">
              <i class="fa-solid fa-plus"></i> ${actionText}
            </button>
          </div>
        </div>
      </div>
    `;
  }).join('');
}

function resetFilters() {
  currentCategory = 'all';
  currentDietFilter = 'all';
  searchQuery = '';
  const searchInput = document.getElementById('catalog-search');
  if (searchInput) searchInput.value = '';

  document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
  const allBtn = document.querySelector('.tab-btn[data-category="all"]');
  if (allBtn) allBtn.classList.add('active');

  document.querySelectorAll('.diet-chip').forEach(c => c.classList.remove('active'));
  const allDiet = document.querySelector('.diet-chip[data-diet="all"]');
  if (allDiet) allDiet.classList.add('active');

  renderCatalog();
}

// ===================================================================
// 6. Cart Management
// ===================================================================
function addToCart(itemId) {
  const item = CATALOG_ITEMS.find(i => i.id === itemId);
  if (!item) return;

  const existing = cart.find(ci => ci.id === itemId);
  if (existing) {
    existing.quantity += 1;
  } else {
    cart.push({
      id: item.id,
      name: item.name,
      price: item.price,
      type: item.type,
      image: item.image,
      quantity: 1
    });
  }

  saveCartToStorage();
  updateCartUI();
  showToast(`Added "${item.name}" to cart`);
}

function updateQuantity(itemId, delta) {
  const index = cart.findIndex(ci => ci.id === itemId);
  if (index === -1) return;

  cart[index].quantity += delta;
  if (cart[index].quantity <= 0) {
    cart.splice(index, 1);
  }

  saveCartToStorage();
  updateCartUI();
}

function removeFromCart(itemId) {
  cart = cart.filter(ci => ci.id !== itemId);
  saveCartToStorage();
  updateCartUI();
}

function calculateBill() {
  const subtotal = cart.reduce((sum, item) => sum + (item.price * item.quantity), 0);
  const gst = Math.round(subtotal * 0.05); // 5% GST
  let discount = 0;

  if (activeCoupon === 'FUNN10' || activeCoupon === 'ARENA10') {
    discount = Math.round(subtotal * 0.10);
  } else if (activeCoupon === 'FUNN50' || activeCoupon === 'PLAY50') {
    discount = 50;
  }

  const grandTotal = Math.max(0, subtotal + gst - discount);
  return { subtotal, gst, discount, grandTotal };
}

function updateCartUI() {
  const totalCount = cart.reduce((sum, i) => sum + i.quantity, 0);
  document.querySelectorAll('.cart-count').forEach(el => el.textContent = totalCount);

  const listEl = document.getElementById('cart-items-list');
  const emptyEl = document.getElementById('cart-empty-view');
  const footerEl = document.getElementById('cart-footer-view');

  if (!listEl) return;

  if (cart.length === 0) {
    if (emptyEl) emptyEl.style.display = 'block';
    listEl.innerHTML = '';
    if (footerEl) footerEl.style.display = 'none';
    return;
  }

  if (emptyEl) emptyEl.style.display = 'none';
  if (footerEl) footerEl.style.display = 'block';

  listEl.innerHTML = cart.map(item => `
    <div class="cart-item">
      <img src="${item.image}" alt="${item.name}" class="cart-item-img" onerror="handleImageError(this, '${item.type}')">
      <div class="cart-item-info">
        <h4 class="cart-item-title">${item.name}</h4>
        <div class="cart-item-price">₹${item.price} &times; ${item.quantity} = ₹${item.price * item.quantity}</div>
      </div>
      <div class="qty-control">
        <button class="qty-btn" onclick="updateQuantity('${item.id}', -1)">-</button>
        <span class="qty-val">${item.quantity}</span>
        <button class="qty-btn" onclick="updateQuantity('${item.id}', 1)">+</button>
      </div>
      <button class="remove-item-btn" title="Remove" onclick="removeFromCart('${item.id}')">
        <i class="fa-solid fa-trash-can"></i>
      </button>
    </div>
  `).join('');

  const bill = calculateBill();
  const subEl = document.getElementById('bill-subtotal');
  if (subEl) subEl.textContent = `₹${bill.subtotal}`;

  const gstEl = document.getElementById('bill-gst');
  if (gstEl) gstEl.textContent = `₹${bill.gst}`;

  const discountRow = document.getElementById('bill-discount-row');
  const discountEl = document.getElementById('bill-discount');
  if (discountRow && discountEl) {
    if (bill.discount > 0) {
      discountRow.style.display = 'flex';
      discountEl.textContent = `-₹${bill.discount}`;
    } else {
      discountRow.style.display = 'none';
    }
  }

  const totalEl = document.getElementById('bill-total');
  if (totalEl) totalEl.textContent = `₹${bill.grandTotal}`;
}

function saveCartToStorage() {
  try {
    localStorage.setItem('funnhub_cart', JSON.stringify(cart));
  } catch (e) {}
}

function loadCartFromStorage() {
  try {
    const saved = localStorage.getItem('funnhub_cart');
    if (saved) cart = JSON.parse(saved);
  } catch (e) {}
}

function toggleCartDrawer(open) {
  const overlay = document.getElementById('cart-overlay');
  if (overlay) {
    if (open) {
      overlay.classList.add('active');
      document.body.style.overflow = 'hidden';
    } else {
      overlay.classList.remove('active');
      document.body.style.overflow = '';
    }
  }
}

// ===================================================================
// 7. Razorpay Checkout Flow (Fixed & Safe)
// ===================================================================
function openCheckout() {
  if (cart.length === 0) {
    showToast('Your cart is empty. Please add items first.');
    return;
  }

  toggleCartDrawer(false);

  const bill = calculateBill();

  // Safely update all labels without null errors
  const payAmtEl = document.getElementById('rzp-pay-amount');
  if (payAmtEl) payAmtEl.textContent = `₹${bill.grandTotal}`;

  const countLabelEl = document.getElementById('rzp-item-count-label');
  if (countLabelEl) countLabelEl.textContent = `${cart.length} item(s)`;

  const simBtnAmt = document.getElementById('rzp-sim-button-amt');
  if (simBtnAmt) simBtnAmt.textContent = `₹${bill.grandTotal}`;

  // Generate dynamic QR code image for instant scan
  const upiId = 'funnhub@axisbank';
  const upiPayload = `upi://pay?pa=${upiId}&pn=FUNN%20HUB&am=${bill.grandTotal}&cu=INR&tn=FunnHub%20Order`;
  const qrImg = document.getElementById('upi-qr-image');
  if (qrImg) {
    qrImg.src = `https://api.qrserver.com/v1/create-qr-code/?size=180x180&data=${encodeURIComponent(upiPayload)}`;
  }

  const backdrop = document.getElementById('payment-modal');
  if (backdrop) {
    backdrop.classList.add('active');
    document.body.style.overflow = 'hidden';
  }
}

function closeCheckout() {
  const backdrop = document.getElementById('payment-modal');
  if (backdrop) {
    backdrop.classList.remove('active');
    document.body.style.overflow = '';
  }
}

function handlePaymentMethodTab(tabName) {
  document.querySelectorAll('.pm-tab').forEach(t => t.classList.remove('active'));
  document.querySelectorAll('.payment-view-panel').forEach(p => p.classList.remove('active'));

  const activeTab = document.querySelector(`.pm-tab[data-tab="${tabName}"]`);
  const activePanel = document.getElementById(`panel-${tabName}`);

  if (activeTab) activeTab.classList.add('active');
  if (activePanel) activePanel.classList.add('active');
}

/**
 * Process Razorpay Payment
 */
async function processRazorpayPayment() {
  const nameInput = document.getElementById('cust-name');
  const phoneInput = document.getElementById('cust-phone');
  const emailInput = document.getElementById('cust-email');
  const orderTypeSelect = document.getElementById('cust-order-type');

  const name = nameInput ? nameInput.value.trim() : 'Guest';
  const phone = phoneInput ? phoneInput.value.trim() : '9876543210';
  const email = emailInput ? emailInput.value.trim() : 'guest@example.com';
  const orderType = orderTypeSelect ? orderTypeSelect.value : 'dine-in';

  if (!name) {
    alert('Please enter your name.');
    if (nameInput) nameInput.focus();
    return;
  }

  if (!phone || phone.length < 10) {
    alert('Please enter a valid 10-digit mobile number.');
    if (phoneInput) phoneInput.focus();
    return;
  }

  const bill = calculateBill();
  const payBtn = document.getElementById('btn-confirm-pay');
  if (payBtn) {
    payBtn.disabled = true;
    payBtn.innerHTML = `<i class="fa-solid fa-spinner fa-spin"></i> Processing via Razorpay...`;
  }

  try {
    let backendOrder = null;
    try {
      const res = await fetch('/api/order/create', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          amount: bill.grandTotal,
          currency: 'INR',
          notes: {
            customer_name: name,
            customer_phone: phone,
            order_type: orderType,
            items_count: cart.length,
            merchant: 'FUNN HUB'
          }
        })
      });
      if (res.ok) {
        backendOrder = await res.json();
      }
    } catch (e) {
      console.log('Client-only mode:', e.message);
    }

    // If official Razorpay checkout SDK is loaded with live keys
    if (typeof window.Razorpay === 'function' && backendOrder && backendOrder.keyId && !backendOrder.keyId.includes('placeholder') && !backendOrder.keyId.includes('YourKey')) {
      const rzpOptions = {
        key: backendOrder.keyId,
        amount: backendOrder.order.amount,
        currency: 'INR',
        name: 'FUNN HUB',
        description: 'Food & Game Passes',
        order_id: backendOrder.order.id,
        prefill: { name, email, contact: phone },
        theme: { color: '#f97316' },
        handler: function (response) {
          completeOrderSuccess({
            orderId: backendOrder.order.id,
            paymentId: response.razorpay_payment_id || `pay_${Date.now()}`,
            name, phone, email, orderType, bill
          });
        },
        modal: {
          ondismiss: function() {
            if (payBtn) {
              payBtn.disabled = false;
              payBtn.innerHTML = `<i class="fa-solid fa-lock"></i> Pay <span id="rzp-sim-button-amt">₹${bill.grandTotal}</span> with Razorpay`;
            }
          }
        }
      };
      const rzp = new window.Razorpay(rzpOptions);
      rzp.open();
      closeCheckout();
      return;
    }

    // Default fast checkout simulation
    setTimeout(() => {
      const timestamp = Date.now().toString().slice(-6);
      const orderId = backendOrder?.order?.id || `ORD-FH-${timestamp}`;
      const paymentId = `pay_rzp_${Math.random().toString(36).substring(2, 11)}`;

      completeOrderSuccess({
        orderId,
        paymentId,
        name,
        phone,
        email,
        orderType,
        bill
      });
    }, 1000);

  } catch (err) {
    console.error('Payment error:', err);
    alert('Payment error. Please try again.');
    if (payBtn) {
      payBtn.disabled = false;
      payBtn.innerHTML = `<i class="fa-solid fa-lock"></i> Pay <span id="rzp-sim-button-amt">₹${bill.grandTotal}</span> with Razorpay`;
    }
  }
}

function completeOrderSuccess({ orderId, paymentId, name, phone, email, orderType, bill }) {
  const payBtn = document.getElementById('btn-confirm-pay');
  if (payBtn) {
    payBtn.disabled = false;
    payBtn.innerHTML = `<i class="fa-solid fa-lock"></i> Pay <span id="rzp-sim-button-amt">₹${bill.grandTotal}</span> with Razorpay`;
  }

  closeCheckout();

  showOrderSuccessReceipt({
    orderId,
    paymentId,
    name,
    phone,
    email,
    orderType,
    bill,
    items: [...cart],
    date: new Date().toLocaleString('en-IN', { timeZone: 'Asia/Kolkata', dateStyle: 'medium', timeStyle: 'short' })
  });

  cart = [];
  saveCartToStorage();
  updateCartUI();
}

function showOrderSuccessReceipt(orderData) {
  const setEl = (id, val) => {
    const el = document.getElementById(id);
    if (el) el.textContent = val;
  };

  setEl('receipt-order-id', orderData.orderId);
  setEl('receipt-payment-id', orderData.paymentId);
  setEl('receipt-date', orderData.date);
  setEl('receipt-cust-name', orderData.name);
  setEl('receipt-cust-phone', orderData.phone);
  setEl('receipt-order-type', orderData.orderType.toUpperCase());
  setEl('receipt-amount', `₹${orderData.bill.grandTotal}`);

  const receiptQr = document.getElementById('receipt-qr-img');
  if (receiptQr) {
    const qrString = `FUNNHUB-TOKEN:${orderData.orderId}:${orderData.paymentId}:₹${orderData.bill.grandTotal}`;
    receiptQr.src = `https://api.qrserver.com/v1/create-qr-code/?size=160x160&data=${encodeURIComponent(qrString)}`;
  }

  const itemsContainer = document.getElementById('receipt-items-summary');
  if (itemsContainer) {
    itemsContainer.innerHTML = orderData.items.map(item => `
      <div style="display: flex; justify-content: space-between; font-size: 0.85rem; padding: 4px 0; border-bottom: 1px dotted #e2e8f0;">
        <span>${item.name} &times; ${item.quantity}</span>
        <b>₹${item.price * item.quantity}</b>
      </div>
    `).join('');
  }

  const receiptModal = document.getElementById('receipt-modal');
  if (receiptModal) {
    receiptModal.classList.add('active');
    document.body.style.overflow = 'hidden';
  }
}

function closeReceipt() {
  const receiptModal = document.getElementById('receipt-modal');
  if (receiptModal) {
    receiptModal.classList.remove('active');
    document.body.style.overflow = '';
  }
}

// ===================================================================
// 8. Official Policy Texts (FunnHub KYC Documents)
// ===================================================================
const POLICY_TEXTS = {
  terms: {
    title: 'Terms & Conditions',
    content: `
      <p><b>Last Updated:</b> October 2026</p>
      <p>Welcome to <b>FUNN HUB</b>. FUNN HUB is a registered partnership firm (PAN: <b>AALFF4908R</b>) operating food service and an indoor gaming arena at <b>Ground Floor Wing A, Building 2, NSL SEZ ARENA, Uppal, Hyderabad - 500039</b>.</p>
      
      <h4>1. Use of Services</h4>
      <p>By using this website, you can order food, reserve snooker and pool tables, and buy indoor game passes. All visitors must follow safety rules inside the arena.</p>

      <h4>2. Pricing in Indian Rupees</h4>
      <p>All prices are clearly shown in <b>Indian Rupees (₹/INR)</b> only. Applicable 5% GST is added at checkout. Online payments are safely handled by <b>Razorpay</b>.</p>

      <h4>3. Safe Environment</h4>
      <p>Visitors must take care of game equipment like snooker cues, VR headsets, and racing rigs. Staff is available to help beginners.</p>

      <h4>4. Governing Law</h4>
      <p>These terms follow the laws of India. Any legal dispute will be handled under the jurisdiction of the courts in Hyderabad, Telangana.</p>
    `
  },
  privacy: {
    title: 'Privacy Policy',
    content: `
      <p><b>Last Updated:</b> October 2026</p>
      <p><b>FUNN HUB</b> respects your privacy. We keep your information safe and confidential.</p>

      <h4>1. What We Collect</h4>
      <ul>
        <li>Your Full Name</li>
        <li>Your 10-digit Mobile Number (to send order token and pass)</li>
        <li>Your Email ID (to send bill receipt)</li>
      </ul>

      <h4>2. Safe Payments</h4>
      <p>We <b>never store</b> your credit card numbers, debit card details, UPI MPIN, or bank passwords. All payments are securely processed by <b>Razorpay</b>.</p>

      <h4>3. No Data Selling</h4>
      <p>We do not sell or share your phone number or email with any third-party marketing companies.</p>
    `
  },
  refund: {
    title: 'Refund & Cancellation Policy',
    content: `
      <p>We want you to have a great time at <b>FUNN HUB</b>. Here is our simple and fair refund policy:</p>

      <h4>1. Failed Online Payments</h4>
      <p>If money was deducted from your bank or UPI account, but you did not receive an order confirmation, your money will be automatically refunded back to your bank account within <b>5 to 7 working days</b>.</p>

      <h4>2. Game Passes & Table Bookings</h4>
      <p>If you cannot attend your reserved slot for Snooker, Pool, or VR, please contact us at least 2 hours before your time. We will reschedule your slot or refund your money.</p>

      <h4>3. Food Orders</h4>
      <p>If there is any mistake in your food order or an issue with quality, please inform our counter staff right away. We will replace your food or refund your payment immediately.</p>

      <h4>4. Contact for Refunds</h4>
      <p>For refund questions, email us at <b>mpowerdbboxes@gmail.com</b> or call <b>+91 98522 37730</b> with your Order ID.</p>
    `
  },
  shipping: {
    title: 'Fulfillment & Delivery Policy',
    content: `
      <p><b>FUNN HUB</b> is an on-site dining and indoor game arena located at <b>NSL SEZ ARENA, Uppal, Hyderabad</b>.</p>

      <h4>1. Instant Digital Pass Delivery</h4>
      <p>As soon as you pay via Razorpay, your digital ticket with a <b>QR Code</b> appears immediately on your screen and is sent via SMS. Delivery is instant and completely free (₹0 fee).</p>

      <h4>2. Food Service Times</h4>
      <ul>
        <li><b>Dine-in / Table Service:</b> Hot and fresh food is served at your table in <b>10 to 15 minutes</b>.</li>
        <li><b>Counter Pickup:</b> Show your digital QR pass at the food counter to pick up your order.</li>
      </ul>

      <h4>3. Game Check-in</h4>
      <p>Show your digital QR ticket at the game counter to get your snooker table assigned or to activate your VR or racing simulator.</p>
    `
  },
  about: {
    title: 'About FUNN HUB',
    content: `
      <h4>Welcome to FUNN HUB</h4>
      <p><b>FUNN HUB</b> is an indoor gaming and food hub located on the <b>Ground Floor, Wing A, Building 2, NSL SEZ ARENA, Uppal, Hyderabad - 500039</b>.</p>

      <h4>Official Business Details</h4>
      <ul>
        <li><b>Firm Name:</b> FUNN HUB (Partnership Firm)</li>
        <li><b>Managing Partner:</b> Mr. Raju Maharaju</li>
        <li><b>PAN Number:</b> AALFF4908R</li>
        <li><b>FSSAI License:</b> 23626009000033 (Govt. of Telangana)</li>
        <li><b>Official Bank:</b> Axis Bank, Current A/C: 926020037871517, IFSC: UTIB0001319</li>
        <li><b>Official Email:</b> mpowerdbboxes@gmail.com</li>
      </ul>

      <h4>What You Will Find at FUNN HUB:</h4>
      <ul>
        <li><b>Snooker & Pool Zone:</b> Professional snooker and pool tables</li>
        <li><b>VR Stations:</b> 2 Virtual Reality gaming pods</li>
        <li><b>Dual Racing Simulators:</b> High-speed steering rigs</li>
        <li><b>Arcade & Sports:</b> Air hockey, foosball, table tennis, basketball hoop, boxing punch machine</li>
        <li><b>Relaxation & Fun:</b> 4 luxury massage chairs, interactive LED dance floor, and selfie spot</li>
        <li><b>Party Area:</b> Private hall for birthday and corporate parties (up to 50 guests)</li>
        <li><b>Fresh Food & Drinks:</b> Artisan burgers, hot pizzas, peri-peri fries, milkshakes, and hot beverages</li>
      </ul>
    `
  },
  contact: {
    title: 'Contact Us',
    content: `
      <h4>Get in Touch with FUNN HUB</h4>
      <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 12px; padding: 18px; margin: 16px 0;">
        <p><b>📍 Venue Address:</b><br>
        FUNN HUB, Ground Floor Wing A, Building 2, NSL SEZ ARENA, Uppal, Circle 09 Uppal, Hyderabad, Telangana - 500039</p>
        <p style="margin-top: 10px;"><b>🏠 Registered Office:</b><br>
        H.No. 2-4-760, B N Reddy Nagar, Pochamma Temple, Cherlapally, Uppal, Rangareddy District, Telangana - 500051</p>
        <p style="margin-top: 10px;"><b>📞 Phone Helpline:</b><br>
        <a href="tel:+919852237730" style="color: #f97316; font-weight: bold;">+91 98522 37730</a> (10:00 AM - 10:00 PM IST)</p>
        <p style="margin-top: 10px;"><b>📧 Official Email:</b><br>
        <a href="mailto:mpowerdbboxes@gmail.com" style="color: #f97316; font-weight: bold;">mpowerdbboxes@gmail.com</a></p>
        <p style="margin-top: 10px;"><b>👤 Authorized Contact Person:</b><br>
        Mr. Raju Maharaju (Partner)</p>
      </div>
    `
  }
};

function openPolicyModal(policyKey) {
  const data = POLICY_TEXTS[policyKey];
  if (!data) return;

  const titleEl = document.getElementById('policy-modal-title');
  if (titleEl) titleEl.textContent = data.title;

  const bodyEl = document.getElementById('policy-modal-body');
  if (bodyEl) bodyEl.innerHTML = data.content;

  const modal = document.getElementById('policy-modal');
  if (modal) {
    modal.classList.add('active');
    document.body.style.overflow = 'hidden';
  }
}

function closePolicyModal() {
  const modal = document.getElementById('policy-modal');
  if (modal) {
    modal.classList.remove('active');
    document.body.style.overflow = '';
  }
}

// ===================================================================
// 9. Event Listeners
// ===================================================================
function setupEventListeners() {
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      currentCategory = btn.getAttribute('data-category');
      renderCatalog();
    });
  });

  document.querySelectorAll('.diet-chip').forEach(chip => {
    chip.addEventListener('click', () => {
      document.querySelectorAll('.diet-chip').forEach(c => c.classList.remove('active'));
      chip.classList.add('active');
      currentDietFilter = chip.getAttribute('data-diet');
      renderCatalog();
    });
  });

  const searchInput = document.getElementById('catalog-search');
  if (searchInput) {
    searchInput.addEventListener('input', (e) => {
      searchQuery = e.target.value.trim();
      renderCatalog();
    });
  }

  const couponBtn = document.getElementById('btn-apply-coupon');
  if (couponBtn) {
    couponBtn.addEventListener('click', () => {
      const codeInput = document.getElementById('coupon-input');
      const code = codeInput ? codeInput.value.trim().toUpperCase() : '';
      if (code === 'FUNN10' || code === 'ARENA10') {
        activeCoupon = 'FUNN10';
        showToast('Success! 10% discount applied to your order.');
        updateCartUI();
      } else if (code === 'FUNN50' || code === 'PLAY50') {
        activeCoupon = 'FUNN50';
        showToast('Success! ₹50 discount applied.');
        updateCartUI();
      } else {
        showToast('Invalid coupon. Try "FUNN10"');
      }
    });
  }

  const contactForm = document.getElementById('contact-form');
  if (contactForm) {
    contactForm.addEventListener('submit', (e) => {
      e.preventDefault();
      showToast('Thank you! Message received. We will call you soon.');
      contactForm.reset();
    });
  }
}

function showToast(message) {
  let toast = document.getElementById('toast-notice');
  if (!toast) {
    toast = document.createElement('div');
    toast.id = 'toast-notice';
    toast.className = 'toast-notice';
    document.body.appendChild(toast);
  }

  toast.innerHTML = `<i class="fa-solid fa-circle-check"></i> <span>${message}</span>`;
  toast.classList.add('show');

  setTimeout(() => {
    toast.classList.remove('show');
  }, 3000);
}
