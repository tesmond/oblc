<script setup lang="ts">
import { useSlideContext } from '@slidev/client'
import { computed } from 'vue'

type WalkthroughStep = {
  title: string
  body: string
  metric?: string
  note?: string
}

const props = defineProps<{
  steps: WalkthroughStep[]
}>()

const { $clicks } = useSlideContext()

const activeIndex = computed(() =>
  Math.min($clicks.value, Math.max(props.steps.length - 1, 0)),
)

const activeStep = computed(() => props.steps[activeIndex.value])
</script>

<template>
  <div class="step-explain" aria-live="polite">
    <Transition name="step-swap" mode="out-in">
      <div :key="activeIndex" class="step-explain__content">
        <div class="step-explain__count">
          Step {{ activeIndex + 1 }} / {{ steps.length }}
        </div>
        <h3>{{ activeStep.title }}</h3>
        <p>{{ activeStep.body }}</p>
        <div v-if="activeStep.metric" class="step-explain__metric">
          {{ activeStep.metric }}
        </div>
        <p v-if="activeStep.note" class="step-explain__note">
          {{ activeStep.note }}
        </p>
      </div>
    </Transition>
  </div>
</template>

<style scoped>
.step-explain {
  min-height: 19rem;
  border-left: 4px solid var(--slidev-theme-primary);
  padding: 1rem 0 1rem 1.4rem;
  display: flex;
  align-items: center;
}

.step-explain__content {
  width: 100%;
}

.step-explain__count {
  color: var(--accent);
  font-size: 0.7rem;
  font-weight: 750;
  letter-spacing: 0.13em;
  text-transform: uppercase;
  margin-bottom: 1rem;
}

h3 {
  font-size: 1.5rem;
  line-height: 1.12;
  margin: 0 0 1rem;
}

p {
  color: #dbe5f5;
  font-size: 1.02rem;
  line-height: 1.55;
  margin: 0;
}

.step-explain__metric {
  color: var(--accent);
  font-size: 1.55rem;
  font-weight: 850;
  margin-top: 1.5rem;
}

.step-explain__note {
  color: var(--muted);
  font-size: 0.8rem;
  margin-top: 0.5rem;
}

.step-swap-enter-active,
.step-swap-leave-active {
  transition: opacity 220ms ease, transform 220ms ease;
}

.step-swap-enter-from {
  opacity: 0;
  transform: translateY(12px);
}

.step-swap-leave-to {
  opacity: 0;
  transform: translateY(-10px);
}
</style>
