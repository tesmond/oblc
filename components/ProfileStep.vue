<script setup lang="ts">
import { useSlideContext } from '@slidev/client'
import { computed } from 'vue'

const { $clicks } = useSlideContext()
const optimized = computed(() => $clicks.value > 0)
</script>

<template>
  <div class="profile-step">
    <Transition name="profile-swap" mode="out-in">
      <img
        :key="optimized ? 'optimized' : 'naive'"
        :src="optimized ? '/4_optimised_python/profile.png' : '/1_naive_python/profile.png'"
        :alt="optimized ? 'Sampling heatmap for the optimized Python worker' : 'Sampling heatmap for naive Python'"
      />
    </Transition>
  </div>
</template>

<style scoped>
.profile-step img {
  width: 100%;
  height: 19rem;
  object-fit: contain;
  object-position: left center;
  border: 1px solid rgba(88, 214, 192, 0.18);
  border-radius: 0.6rem;
  background: #0c121c;
}

.profile-step__hint {
  color: var(--muted);
  font-size: 0.68rem;
  margin-top: 0.5rem;
  text-align: right;
}

.profile-swap-enter-active,
.profile-swap-leave-active {
  transition: opacity 220ms ease, transform 220ms ease;
}

.profile-swap-enter-from { opacity: 0; transform: translateY(10px); }
.profile-swap-leave-to { opacity: 0; transform: translateY(-8px); }
</style>
